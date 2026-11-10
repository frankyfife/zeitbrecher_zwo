#include "levelfix.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "hf2.h"
#include "log.h"

namespace host {

namespace {

using LevelTickFn = void (*)(void*, void*);
using NodeCbFn = void (*)(void*, int);
using ObjByIdFn = uint8_t* (*)(int);

const Ts2Image* g_img = nullptr;
LevelTickFn g_tick = nullptr;
int g_level_ticks = 0;

// ---- Test (TS2_RFTEST="takt:befehl;..."; Takt = Aufrufe der Level-Aktualisierung)
struct Cmd {
    int tick;
    std::string what;
};
std::vector<Cmd> g_cmds;

uint8_t* player1() { return *g_img->va<uint8_t*>(hf2::kPlayerTablePtr); }
uint8_t* player_char() {
    uint8_t* table = player1();
    if (!table) return nullptr;
    uint8_t* sub = *reinterpret_cast<uint8_t**>(table + hf2::kPlayerSub);
    return sub ? *reinterpret_cast<uint8_t**>(sub + hf2::kCharSub) : nullptr;
}
using FindRoomFn = int32_t (*)(const float*, float, float);

int id_of(const uint8_t* obj) {
    const int n = *g_img->va<int32_t>(hf2::kRfObjCount);
    const uint8_t* t = g_img->va<uint8_t>(hf2::kRfObjTable);
    for (int i = 0; i < n; ++i)
        if (*reinterpret_cast<uint8_t* const*>(t + i * 16 + 8) == obj) return *reinterpret_cast<const int32_t*>(t + i * 16);
    return -1;
}

const float* pad_pos(int id) {
    uint8_t* pads = *g_img->va<uint8_t*>(hf2::kRfPads);
    const int n = *g_img->va<int32_t>(hf2::kRfPadCount);
    for (int i = 0; pads && i < n; ++i)
        if (*reinterpret_cast<int32_t*>(pads + i * 32) == id) return reinterpret_cast<float*>(pads + i * 32 + 0xC);
    return nullptr;
}

void info() {
    const int n = *g_img->va<int32_t>(hf2::kRfNodeCount);
    auto** nodes = g_img->va<uint8_t*>(hf2::kRfNodes);
    log("[rf] Takt %d, %d Knoten, Kameramodus %d\n", g_level_ticks, n, *g_img->va<int32_t>(hf2::kCameraMode));
    for (int i = 0; i < n && i < 8; ++i) {
        uint8_t* o = nodes[i];
        uint8_t* d = *reinterpret_cast<uint8_t**>(o + 0x140);
        log("[rf]   Knoten %d: %p id %#x Leben %.2f Zustand %d cb %p\n", i, static_cast<void*>(o), id_of(o),
            *reinterpret_cast<float*>(o + 0x204), d ? *reinterpret_cast<int32_t*>(d + 0xEC) : -1,
            *reinterpret_cast<void**>(o + 0x158));
    }
    if (uint8_t* c = player_char()) {
        uint8_t* pl = player1();
        const float* p = reinterpret_cast<float*>(pl + hf2::kPlayerPos);
        uint8_t* cam = *reinterpret_cast<uint8_t**>(pl + 8);
        log("[rf]   Spieler %.2f %.2f %.2f Gier %.2f Raum %d, Pad-Index %d, Kamera %d an %p, Bewegung %d\n", p[0], p[1],
            p[2], *reinterpret_cast<float*>(pl + 0x154), *reinterpret_cast<int32_t*>(pl + 0xD78),
            *reinterpret_cast<int32_t*>(c + 0x1118), cam ? *reinterpret_cast<int32_t*>(cam + 0x22C) : -1,
            cam ? *reinterpret_cast<void**>(cam + 0x2C8) : nullptr, *reinterpret_cast<int32_t*>(pl + 0x164));
    }
    // Skripte (Liste 0x142F27A28, +0x20 nächstes, +0x30 Nummer)
    for (uint8_t* s = *g_img->va<uint8_t*>(0x142F27A28); s; s = *reinterpret_cast<uint8_t**>(s + 0x20)) {
        const int id = *reinterpret_cast<int32_t*>(s + 0x30);
        if (id < 6 || id > 10) continue;
        auto** o = reinterpret_cast<uint8_t**>(s + 0xB0);
        auto** e = reinterpret_cast<uint8_t**>(s + 0xD0);
        log("[rf]   Skript %d %p: +34 %d +3C %#x +1B8 %d obj %p %p %p %p ton %p(%d) %p(%d) %p %p\n", id,
            static_cast<void*>(s), *reinterpret_cast<int32_t*>(s + 0x34), *reinterpret_cast<uint32_t*>(s + 0x3C),
            *reinterpret_cast<int32_t*>(s + 0x1B8), o[0], o[1], o[2], o[3], e[0], e[0] ? *reinterpret_cast<int32_t*>(e[0]) : 0,
            e[1], e[1] ? *reinterpret_cast<int32_t*>(e[1]) : 0, e[2], e[3]);
    }
    // Ring-Speicher (0x1411AA11C): Blöcke {Zustand, Größe, Vorgänger, Besitzer, Rückruf}
    {
        uint8_t* cur = *g_img->va<uint8_t*>(0x142F48958);
        uint8_t* beg = *g_img->va<uint8_t*>(0x142F48960);
        uint8_t* end = *g_img->va<uint8_t*>(0x142F48968);
        int n[4] = {}, bad = 0, steps = 0;
        int64_t sz[4] = {};
        bool cur_ok = false;
        int prev = 0, badprev = 0, adjfree = 0, last = -1;
        for (uint8_t* p = beg; p && p < end && steps < 100000; ++steps) {
            const int st = *reinterpret_cast<int32_t*>(p), s = *reinterpret_cast<int32_t*>(p + 4);
            if (p == cur) cur_ok = true;
            if (st < 0 || st > 2 || s <= 0) { ++bad; break; }
            if (p != beg && *reinterpret_cast<int32_t*>(p + 8) != prev) ++badprev;
            if (st == 0 && last == 0) ++adjfree;
            ++n[st], sz[st] += s, prev = s, last = st;
            p += s;
        }
        log("[rf]   Ring: Anfang %p Ende %p Zeiger %p%s, frei %d (Zähler %d, Kapazität %d), Blöcke frei %d/%lld fest %d/%lld "
            "räumbar %d/%lld, Fehler %d, Vorgänger falsch %d, freie Nachbarn %d\n",
            static_cast<void*>(beg), static_cast<void*>(end), static_cast<void*>(cur), cur_ok ? "" : " (KEIN BLOCK)",
            int(sz[0]), *g_img->va<int32_t>(0x142F48944), *g_img->va<int32_t>(0x142F48948), n[0], (long long)sz[0], n[2],
            (long long)sz[2], n[1], (long long)sz[1], bad, badprev, adjfree);
    }
    std::string g;
    char buf[32];
    for (uint64_t a = 0x143F27400; a < 0x143F27460; a += 4) {
        std::snprintf(buf, sizeof buf, " %X:%d", unsigned(a & 0xFFF), *g_img->va<int32_t>(a));
        g += buf;
    }
    log("[rf]   Globale%s\n", g.c_str());
}

void run(const std::string& c) {
    log("[rf] Befehl %s (Takt %d)\n", c.c_str(), g_level_ticks);
    if (c == "i") return info();
    if (c[0] == 'k') {
        const int i = std::atoi(c.c_str() + 1);
        if (i >= *g_img->va<int32_t>(hf2::kRfNodeCount)) return;
        uint8_t* o = g_img->va<uint8_t*>(hf2::kRfNodes)[i];
        *reinterpret_cast<float*>(o + 0x204) = 0.0f;
        if (auto cb = *reinterpret_cast<NodeCbFn*>(o + 0x158)) cb(o, 0);
        return;
    }
    // t x/y/z[/gier], p<pad-id hex>: Spieler versetzen (Raum neu suchen)
    if (c[0] == 't' || c[0] == 'p') {
        float q[4] = {};
        int n = 0;
        if (c[0] == 'p') {
            const float* pp = pad_pos(int(std::strtol(c.c_str() + 1, nullptr, 16)));
            if (!pp) return log("[rf]   Pad fehlt\n");
            q[0] = pp[0], q[1] = pp[1], q[2] = pp[2], n = 3;
        } else {
            n = std::sscanf(c.c_str() + 1, "%f/%f/%f/%f", &q[0], &q[1], &q[2], &q[3]);
        }
        uint8_t* pl = player1();
        if (!pl || n < 3) return;
        float* p = reinterpret_cast<float*>(pl + hf2::kPlayerPos);
        p[0] = q[0], p[1] = q[1], p[2] = q[2];
        if (n == 4) *reinterpret_cast<float*>(pl + 0x154) = q[3];
        const auto find_room = reinterpret_cast<FindRoomFn>(g_img->va<uint8_t>(hf2::kFindRoom));
        const int32_t room = find_room(p, 1.0f, *reinterpret_cast<float*>(pl + hf2::kPlayerRoomArg));
        if (room) *reinterpret_cast<int32_t*>(pl + 0xD78) = room;
        log("[rf]   versetzt nach %.2f %.2f %.2f, Raum %d\n", p[0], p[1], p[2], room);
        return;
    }
    if (c[0] == 'y') *reinterpret_cast<float*>(player1() + 0x154) = float(std::atof(c.c_str() + 1));
    // d<n>[/schaden]: Knoten n über TS2s Schadensweg treffen (0x1411E810C, wie
    // das ElectroTool 0x14116E505); s[/schaden]: alle Schildgeneratoren
    if (c[0] == 'd' || c[0] == 's') {
        float dmg = 10.0f;
        int n = 0;
        if (c[0] == 'd') std::sscanf(c.c_str() + 1, "%d/%f", &n, &dmg);
        else std::sscanf(c.c_str() + 1, "/%f", &dmg);
        std::vector<uint8_t*> targets;
        if (c[0] == 'd') {
            if (n < *g_img->va<int32_t>(hf2::kRfNodeCount)) targets.push_back(g_img->va<uint8_t*>(hf2::kRfNodes)[n]);
        } else {
            for (int i = 0; i < *g_img->va<int32_t>(0x143F27410); ++i) targets.push_back(g_img->va<uint8_t*>(0x143F273F8)[i]);
        }
        uint8_t* src = *reinterpret_cast<uint8_t**>(player1() + hf2::kPlayerSub);
        using HitInitFn = void (*)(void*);
        using DamageFn = void (*)(void*);
        for (uint8_t* t : targets) {
            alignas(16) uint8_t hit[0x80] = {};
            float pos[4] = {};
            std::memcpy(pos, t + 0x6C, 12);
            reinterpret_cast<HitInitFn>(g_img->va<uint8_t>(0x1411E5C10))(hit);
            *reinterpret_cast<uint8_t**>(hit) = t;
            *reinterpret_cast<uint8_t**>(hit + 8) = src;
            *reinterpret_cast<uint8_t**>(hit + 0x10) = src;
            *reinterpret_cast<int32_t*>(hit + 0x2C) = -1;
            *reinterpret_cast<float*>(hit + 0x30) = dmg;
            *reinterpret_cast<int32_t*>(hit + 0x34) = 0x18;
            *reinterpret_cast<float**>(hit + 0x38) = pos;
            const float before = *reinterpret_cast<float*>(t + 0x204);
            reinterpret_cast<DamageFn>(g_img->va<uint8_t>(0x1411E810C))(hit);
            log("[rf]   Treffer %p (id %#x, Handler %p): Leben %.2f -> %.2f\n", static_cast<void*>(t), id_of(t),
                *reinterpret_cast<void**>(t + 0x1D0), before, *reinterpret_cast<float*>(t + 0x204));
        }
        return;
    }
    // x<anzahl>[/art]: Explosionen (0x14111829C, wie beim Tod eines Knotens) an Knoten 3
    if (c[0] == 'x') {
        int cnt = 1, kind = 6;
        std::sscanf(c.c_str() + 1, "%d/%d", &cnt, &kind);
        uint8_t* t = g_img->va<uint8_t*>(hf2::kRfNodes)[3];
        using ExplFn = void (*)(int, int, const float*, int, void*, void*);
        for (int i = 0; i < cnt; ++i) {
            float pos[4] = {};
            std::memcpy(pos, t + 0x6C, 12);
            pos[0] += float(i % 5) * 0.7f, pos[2] += float(i / 5) * 0.7f;
            reinterpret_cast<ExplFn>(g_img->va<uint8_t>(0x14111829C))(kind, 0x15, pos, 0, nullptr, nullptr);
        }
        return;
    }
    if (c[0] == 'q') {
        const int id = int(std::strtol(c.c_str() + 1, nullptr, 16));
        if (const float* q = pad_pos(id)) log("[rf]   Pad %#x bei %.2f %.2f %.2f\n", id, q[0], q[1], q[2]);
    }
    // n x/z/r: Pads im Umkreis
    if (c[0] == 'n') {
        float x = 0, z = 0, r = 0;
        std::sscanf(c.c_str() + 1, "%f/%f/%f", &x, &z, &r);
        uint8_t* pads = *g_img->va<uint8_t*>(hf2::kRfPads);
        const int np = *g_img->va<int32_t>(hf2::kRfPadCount);
        for (int i = 0; pads && i < np; ++i) {
            const float* q = reinterpret_cast<float*>(pads + i * 32 + 0xC);
            if ((q[0] - x) * (q[0] - x) + (q[2] - z) * (q[2] - z) <= r * r)
                log("[rf]   Pad %#x (%d) %.2f %.2f %.2f Raum %d Gruppe %d\n", *reinterpret_cast<int32_t*>(pads + i * 32), i,
                    q[0], q[1], q[2], *reinterpret_cast<int16_t*>(pads + i * 32 + 0xA),
                    *reinterpret_cast<int16_t*>(pads + i * 32 + 8));
        }
    }
}

void level_tick(void* a, void* b) {
    g_tick(a, b);
    ++g_level_ticks;
    for (const Cmd& c : g_cmds)
        if (c.tick == g_level_ticks) run(c.what);
}

}  // namespace

void levelfix_install(const Ts2Image& img) {
    g_img = &img;
    char spec[1024] = {};
    if (GetEnvironmentVariableA("TS2_RFTEST", spec, sizeof spec) > 0) {
        for (char* tok = std::strtok(spec, ";,"); tok; tok = std::strtok(nullptr, ";,")) {
            int t = 0;
            char what[64] = {};
            if (std::sscanf(tok, "%d:%63s", &t, what) == 2) g_cmds.push_back({t, what});
        }
        LevelTickFn* slot = img.va<LevelTickFn>(hf2::kRfLevelTickSlot);
        DWORD old;
        VirtualProtect(slot, 8, PAGE_READWRITE, &old);
        g_tick = *slot;
        *slot = &level_tick;
        VirtualProtect(slot, 8, old, &old);
        log("[rf] Test: %zu Befehle, Level-Aktualisierung %p umgelenkt\n", g_cmds.size(), reinterpret_cast<void*>(g_tick));
    }
}

}  // namespace host
