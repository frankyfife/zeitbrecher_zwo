#include "features.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <fstream>

#include "hf2.h"
#include "input.h"
#include "log.h"

namespace host {

Settings load_settings(const std::filesystem::path& ini) {
    if (!std::filesystem::exists(ini)) {
        std::ofstream f(ini);
        f << "; Einstellungen für ts2.exe\n"
             "[waffen]\n"
             "; Tempo des Waffenwechsels in Prozent (100 = wie das Spiel; gilt auch für die KI)\n"
             "wechseltempo=100\n\n"
             "[bewegung]\n"
             "; Springen aus dem Stand (erste Fassung)\n"
             "springen=1\n"
             "; virtueller Tastencode, 32 = Leertaste\n"
             "sprungtaste=32\n"
             "; Sprunghöhe in Spieleinheiten\n"
             "sprunghoehe=1.2\n";
    }
    Settings s;
    const std::wstring p = ini.wstring();
    s.width = int(GetPrivateProfileIntW(L"grafik", L"breite", 1920, p.c_str()));
    s.height = int(GetPrivateProfileIntW(L"grafik", L"hoehe", 1080, p.c_str()));
    s.fullscreen = GetPrivateProfileIntW(L"grafik", L"vollbild", 0, p.c_str()) != 0;
    s.vsync = GetPrivateProfileIntW(L"grafik", L"bildsync", 0, p.c_str()) != 0;
    s.weapon_switch_percent = int(GetPrivateProfileIntW(L"waffen", L"wechseltempo", 100, p.c_str()));
    s.jump = GetPrivateProfileIntW(L"bewegung", L"springen", 1, p.c_str()) != 0;
    s.jump_key = int(GetPrivateProfileIntW(L"bewegung", L"sprungtaste", 0x20, p.c_str()));
    wchar_t buf[64];
    GetPrivateProfileStringW(L"bewegung", L"sprunghoehe", L"1.2", buf, 64, p.c_str());
    s.jump_height = float(_wtof(buf));
    return s;
}

namespace {

uint8_t* player1(const Ts2Image& img) {
    uint8_t* table = *img.va<uint8_t*>(hf2::kPlayerTablePtr);
    if (!table || *img.va<int32_t>(hf2::kPlayerCount) <= 0) return nullptr;
    return table;
}

uint8_t* character_of(uint8_t* player) {
    auto* sub = *reinterpret_cast<uint8_t**>(player + hf2::kPlayerSub);
    return sub ? *reinterpret_cast<uint8_t**>(sub + hf2::kCharSub) : nullptr;
}

bool g_jump_was_down = false;

}  // namespace

// Waffenwechsel: TS2 liest das Tempo (20/7 je Sekunde) an genau einer Stelle
// (0x14112D4A0); wie im Xbox-Fork wird die Konstante einmal skaliert.
void features_install(const Ts2Image& img, const Settings& s) {
    // Render-Auflösung: die festen 1920/1080 im Aufruf des DX11-Setups ersetzen.
    auto* w = img.va<uint32_t>(hf2::kResWidthImm);
    auto* h = img.va<uint32_t>(hf2::kResHeightImm);
    if (*w == 1920 && *h == 1080 && (s.width != 1920 || s.height != 1080)) {
        DWORD old;
        VirtualProtect(w, 16, PAGE_EXECUTE_READWRITE, &old);
        *w = uint32_t(s.width);
        *h = uint32_t(s.height);
        VirtualProtect(w, 16, old, &old);
        FlushInstructionCache(GetCurrentProcess(), w, 16);
        log("Render-Auflösung %dx%d\n", s.width, s.height);
    }
    const int pct = s.weapon_switch_percent;
    if (pct == 100 || pct <= 0) return;
    float* rate = img.va<float>(hf2::kWeaponSwitchRate);
    if (*rate != hf2::kWeaponSwitchRateValue) {
        log("Waffenwechsel: unerwarteter Wert %f, nicht geändert\n", *rate);
        return;
    }
    DWORD old = 0;
    VirtualProtect(rate, sizeof(float), PAGE_READWRITE, &old);
    *rate = hf2::kWeaponSwitchRateValue * float(pct) / 100.0f;
    VirtualProtect(rate, sizeof(float), old, &old);
    log("Waffenwechsel: %d %% (%.3f je Sekunde)\n", pct, *rate);
}

// Springen: Bewegungsmodus 2 (Fallen, 0x1411DA36C) vom Boden aus mit einer
// Aufwärtsgeschwindigkeit betreten; der Fall-Code zieht je Takt 0,01 ab und
// die Kollision landet den Spieler wieder in Modus 0. v0 = sqrt(2 g h).
// Die Laufgeschwindigkeit des Augenblicks wird mitgenommen (wie im Xbox-Fork).
void features_tick(const Ts2Image& img, const Settings& s, void* window) {
    // Bewegungsmodus von Spieler 1 bei jedem Wechsel protokollieren (Prüfung des Sprungs)
    static int32_t last_mode = -1;
    if (uint8_t* p1 = player1(img)) {
        const int32_t m = *reinterpret_cast<int32_t*>(p1 + hf2::kPlayerMoveMode);
        if (m != last_mode) {
            uint8_t* c = character_of(p1);
            log("[spieler] Bewegungsmodus %d -> %d%s", last_mode, m, c ? "" : "\n");
            if (c) log(", Vertikalgeschwindigkeit %.3f\n", *reinterpret_cast<float*>(c + hf2::kCharVerticalSpeed));
            last_mode = m;
        }
    }
    // Lage von Spieler 1 je Takt merken: daraus die Laufgeschwindigkeit
    uint8_t* p = player1(img);
    const float ticks = *img.va<float>(hf2::kFrameTicks);
    float* pos = p ? reinterpret_cast<float*>(p + hf2::kPlayerPos) : nullptr;
    static float last[3];
    static bool have_last = false;
    static int trace = 0;  // Takte, für die die Flugbahn protokolliert wird
    if (pos && trace > 0 && ticks > 0.0f) {
        if (--trace % 4 == 0)
            log("[sprung]   %.3f %.3f %.3f Modus %d\n", pos[0], pos[1], pos[2],
                *reinterpret_cast<int32_t*>(p + hf2::kPlayerMoveMode));
    }
    const bool down = s.jump && (((GetAsyncKeyState(s.jump_key) & 0x8000) != 0 && GetForegroundWindow() == HWND(window)) ||
                                 (input_host_bits() & 0x80000000u));
    const bool pressed = down && !g_jump_was_down;
    g_jump_was_down = down;
    if (pressed && pos && have_last && ticks > 0.0f) {
        auto* mode = reinterpret_cast<int32_t*>(p + hf2::kPlayerMoveMode);
        uint8_t* chr = character_of(p);
        if (*mode == 0 && chr) {
            // wie der Gehcode beim Übergang ins Fallen (0x1411DA03B): Modus 2,
            // Bit 0x800000 in +0x10, Quergeschwindigkeit je Takt; dazu die
            // Fallhöhe ab hier und die Aufwärtsgeschwindigkeit. Ohne das
            // setzt der Fall die Quergeschwindigkeit des letzten Falls fort.
            auto* vx = reinterpret_cast<float*>(p + hf2::kPlayerFallSpeedX);
            auto* vz = reinterpret_cast<float*>(p + hf2::kPlayerFallSpeedZ);
            const float old_x = *vx, old_z = *vz;
            *vx = (pos[0] - last[0]) / ticks;
            *vz = (pos[2] - last[2]) / ticks;
            // Laufen ist ≈ 0,1 je Takt; mehr ist ein Sprung der Lage (Level,
            // Teleport) und keine Geschwindigkeit
            if (!(std::fabs(*vx) <= 1.0f && std::fabs(*vz) <= 1.0f)) *vx = *vz = 0.0f;
            const float v0 = std::sqrt(2.0f * 0.01f * s.jump_height);
            *reinterpret_cast<float*>(chr + hf2::kCharVerticalSpeed) = v0;
            *reinterpret_cast<float*>(chr + hf2::kCharFallStartY) = pos[1];
            *reinterpret_cast<uint32_t*>(p + hf2::kPlayerFlags10) |= 0x800000u;
            *mode = 2;
            log("[sprung] ab %.3f %.3f %.3f, hoch %.3f, quer %.3f %.3f (vorher %.3f %.3f), +0xDB8 %p\n", pos[0], pos[1],
                pos[2], v0, *vx, *vz, old_x, old_z, *reinterpret_cast<void**>(p + 0xDB8));
            trace = 120;
        }
    }
    if (!pos) {
        have_last = false;
    } else if (ticks > 0.0f) {
        std::memcpy(last, pos, sizeof last);
        have_last = true;
    }
}

}  // namespace host
