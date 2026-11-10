#include "profile.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "hf2.h"
#include "hooks.h"
#include "log.h"

namespace host {

namespace {

// Freischalt-Bedingung wie im PC-Port (Xbox: dieselben Felder in 0x14 Byte,
// ohne die Lücken; c steht dort bei +0x10)
struct Cond {
    int32_t type, a, b, pad;
    const void* list;
    int32_t c, pad2;
};
static_assert(sizeof(Cond) == 0x20, "Bedingung: 0x20 Byte");

using EvalFn = int (*)(const void* cond, int mode);
using IntIntFn = int (*)(int, int);
using IntFn = int (*)(int);
using EntryFn = uint8_t* (*)(int);
using CreateFn = int (*)(const char* name);
using LevelEndFn = uint64_t (*)(void*, void*, void*, void*);

struct Api {
    EvalFn eval;
    IntIntFn medal_any;
    IntFn set_open;
    EntryFn set_entry, char_entry;
    CreateFn create;
    LevelEndFn level_end;
    const char* flags;
    const int32_t* all;
    uint8_t* const* table;
    const int32_t* player_profile;
    const uint8_t *mode_conds, *map_conds, *char_table;
    const void *mode16_map5, *mode16_map8;
} g_api;

std::filesystem::path g_file;
std::vector<uint8_t> g_saved;  // Stand der Datei (bzw. des frisch geladenen Profils)

// ---- Freischalt-Regeln der Xbox-Fassung (Adressen dort in hf2.h)

// Spielart x (Arcade): Bedingung aus der Tabelle; Flag 1 = alles in Arcade
int unlock_mode(int x) {
    if (g_api.flags[hf2::kUnlockArcade]) return 1;
    if (x < 0 || x >= 19) return 0;
    return g_api.eval(g_api.mode_conds + 0x20 * x, 0);
}

// Story-Level (Index 0–9) auf Schwierigkeit diff: das erste immer, sonst
// das vorige auf dieser oder einer höheren Schwierigkeit geschafft (Art 2,
// Modus 1 prüft nur P+0xBC8 + koop*0xC + diff*4)
int unlock_story(int level, int diff, int coop) {
    if (g_api.flags[hf2::kUnlockStory] || level <= 0) return 1;
    const Cond c{2, level - 1, diff, 0, nullptr, coop, 0};
    return g_api.eval(&c, 1);
}

// Arcade-Karte x (19 Stück)
int unlock_map(int x) {
    if (g_api.flags[hf2::kUnlockArcade]) return 1;
    if (x < 0 || x >= 19) return 0;
    return g_api.eval(g_api.map_conds + hf2::kMapStride * x, 0);
}

// Kartenliste der Spielart 0x10: Karte 3 immer, 5 und 8 mit eigener Bedingung
int unlock_map16(int x) {
    if (g_api.flags[hf2::kUnlockArcade]) return 1;
    switch (x) {
        case 3: return 1;
        case 5: return g_api.eval(g_api.mode16_map5, 0);
        case 8: return g_api.eval(g_api.mode16_map8, 0);
        default: return 0;
    }
}

// Figur id: Bedingung +0x10 im Figureneintrag; Flag 3 = alle Figuren
int unlock_char(int id) {
    const uint8_t* e = g_api.char_entry(id);
    if (!e) return 0;
    const auto* cond = reinterpret_cast<const int32_t*>(e + 0x10);
    if (*cond == 0) return 0;
    if (g_api.flags[hf2::kUnlockChars]) return 1;
    return g_api.eval(cond, 0);
}

// Challenge chal im Satz set: der Satz offen und jede Challenge davor mit Medaille
int unlock_challenge(int chal, int set) {
    if (*g_api.all) return 1;
    if (g_api.flags[chal < 0x15 ? hf2::kUnlockChallenge : hf2::kUnlockArcade]) return 1;
    if (!g_api.set_open(set)) return 0;
    const uint8_t* s = g_api.set_entry(set);
    const int n = *reinterpret_cast<const int32_t*>(s + 4);
    const auto* ids = reinterpret_cast<const int32_t*>(s + 8);
    for (int i = 0; i < n; ++i) {
        if (ids[i] == chal) return 1;
        if (!g_api.medal_any(ids[i], -1)) return 0;
    }
    return 0;
}

// Die Homefront-Listen blenden Gesperrtes aus; die Xbox zeigt es ausgegraut
int show_entry(int) { return 1; }
int show_story_level(int, int, int) { return 1; }

// Funktionsanfang durch jmp [rip] auf eigenen Code ersetzen (14 Byte)
bool replace_function(const Ts2Image& img, uint64_t va, const uint8_t (&expect)[8], const void* fn) {
    uint8_t* at = img.va(va);
    if (std::memcmp(at, expect, sizeof expect) != 0) {
        log("[fortschritt] 0x%llX: andere Bytes, nicht ersetzt\n", static_cast<unsigned long long>(va));
        return false;
    }
    uint8_t code[14] = {0xFF, 0x25, 0, 0, 0, 0};
    const uint64_t to = reinterpret_cast<uint64_t>(fn);
    std::memcpy(code + 6, &to, 8);
    DWORD old;
    VirtualProtect(at, sizeof code, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(at, code, sizeof code);
    VirtualProtect(at, sizeof code, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, sizeof code);
    return true;
}

// ---- Profil in der Datei

uint8_t* profile_entry(int idx) {
    uint8_t* t = *g_api.table;
    if (!t || idx < 0 || idx >= int(hf2::kProfileCount)) return nullptr;
    return t + size_t(idx) * hf2::kProfileSize;
}

// TS2_STORY_GESCHAFFT=<index>[:<schwierigkeit>] (Test): Story-Level als
// geschafft eintragen wie das Levelende (0x14110B520, ein Spieler)
void test_story_done(uint8_t* entry) {
    char v[32] = {};
    if (GetEnvironmentVariableA("TS2_STORY_GESCHAFFT", v, sizeof v) == 0) return;
    const int level = std::atoi(v);
    const char* colon = std::strchr(v, ':');
    const int diff = colon ? std::atoi(colon + 1) : 0;
    if (level < 0 || level > 9 || diff < 0 || diff > 2) return;
    auto* bits = reinterpret_cast<uint32_t*>(entry + hf2::kProfileProgress + hf2::kProgressStory);
    for (int d = 0; d <= diff; ++d) bits[d] |= 1u << level;
    log("[fortschritt] Test: Story-Level %d auf Schwierigkeit %d als geschafft eingetragen\n", level, diff);
}

// Neues Profil beim Start (0x14110E7C4): danach den gespeicherten Stand
// hineinkopieren; den Namen vergibt TS2 selbst
int create_hook(const char* name) {
    const int idx = g_api.create(name);
    uint8_t* e = profile_entry(idx);
    if (!e) return idx;
    std::ifstream f(g_file, std::ios::binary);
    if (f) {
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (data.size() == hf2::kProfileSize) {
            std::memcpy(e + 0x20, data.data() + 0x20, hf2::kProfileSize - 0x20);
            const auto* story = reinterpret_cast<const uint32_t*>(e + hf2::kProfileProgress + hf2::kProgressStory);
            log("[fortschritt] Profil %d (\"%s\") aus %ls geladen, Story geschafft %03X/%03X/%03X\n", idx,
                reinterpret_cast<const char*>(e), g_file.c_str(), story[0], story[1], story[2]);
        } else {
            log("[fortschritt] %ls: %zu Byte statt %u, nicht geladen\n", g_file.c_str(), data.size(),
                hf2::kProfileSize);
        }
    } else {
        log("[fortschritt] kein %ls, neues Profil %d (\"%s\")\n", g_file.c_str(), idx,
            reinterpret_cast<const char*>(e));
    }
    g_saved.assign(e, e + hf2::kProfileSize);
    test_story_done(e);
    return idx;
}

uint64_t level_end_hook(void* a, void* b, void* c, void* d) {
    const uint64_t r = g_api.level_end(a, b, c, d);
    profile_save();
    return r;
}

}  // namespace

void profile_save() {
    if (g_file.empty() || g_saved.empty()) return;  // kein Profil geladen/angelegt
    const uint8_t* e = profile_entry(g_api.player_profile[0]);
    if (!e || std::memcmp(e, g_saved.data(), hf2::kProfileSize) == 0) return;
    std::filesystem::path tmp = g_file;
    tmp += L".neu";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(e), hf2::kProfileSize);
        if (!f) {
            log("[fortschritt] %ls nicht schreibbar\n", tmp.c_str());
            return;
        }
    }
    if (!MoveFileExW(tmp.c_str(), g_file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        log("[fortschritt] %ls nicht ersetzt (%lu)\n", g_file.c_str(), GetLastError());
        return;
    }
    g_saved.assign(e, e + hf2::kProfileSize);
    const auto* story = reinterpret_cast<const uint32_t*>(e + hf2::kProfileProgress + hf2::kProgressStory);
    log("[fortschritt] Profil gespeichert, Story geschafft %03X/%03X/%03X\n", story[0], story[1], story[2]);
}

void profile_install(const Ts2Image& img, const std::filesystem::path& file) {
    auto fn = [&](uint64_t va) { return img.va<void>(va); };
    g_api.eval = reinterpret_cast<EvalFn>(fn(hf2::kCondEval));
    g_api.medal_any = reinterpret_cast<IntIntFn>(fn(hf2::kMedalAny));
    g_api.set_open = reinterpret_cast<IntFn>(fn(hf2::kChallengeSetOpen));
    g_api.set_entry = reinterpret_cast<EntryFn>(fn(hf2::kChallengeSet));
    g_api.char_entry = reinterpret_cast<EntryFn>(fn(hf2::kCharEntry));
    g_api.flags = img.va<const char>(hf2::kUnlockFlags);
    g_api.all = img.va<const int32_t>(hf2::kUnlockAll);
    g_api.table = img.va<uint8_t* const>(hf2::kProfileTable);
    g_api.player_profile = img.va<const int32_t>(hf2::kPlayerProfile);
    g_api.mode_conds = img.va<const uint8_t>(hf2::kModeConds);
    g_api.map_conds = img.va<const uint8_t>(hf2::kMapConds);
    g_api.char_table = img.va<const uint8_t>(hf2::kCharTable);
    g_api.mode16_map5 = img.va<const void>(hf2::kMode16Map5);
    g_api.mode16_map8 = img.va<const void>(hf2::kMode16Map8);

    // erst die Aufrufstellen (sie prüfen noch das alte Ziel), dann die Funktionen
    int sites = 0;
    for (uint64_t va : hf2::kMapListShowCalls)
        sites += hook_call(img, va, hf2::kUnlockMapFn, reinterpret_cast<const void*>(&show_entry)) != nullptr;
    for (uint64_t va : hf2::kMapMode16Calls)
        sites += hook_call(img, va, hf2::kUnlockMapFn, reinterpret_cast<const void*>(&unlock_map16)) != nullptr;
    sites += hook_call(img, hf2::kStoryListShowCall, hf2::kUnlockStoryLevelFn,
                       reinterpret_cast<const void*>(&show_story_level)) != nullptr;

    static constexpr uint8_t kMode[8] = {0x48, 0x83, 0xEC, 0x28, 0xB9, 0x01, 0x00, 0x00};
    static constexpr uint8_t kStory[8] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xD9};
    static constexpr uint8_t kMap[8] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xD9};
    static constexpr uint8_t kChar[8] = {0x48, 0x83, 0xEC, 0x28, 0xE8, 0x5B, 0x2C, 0xFA};
    static constexpr uint8_t kChallenge[8] = {0x48, 0x83, 0xEC, 0x28, 0x8B, 0xCA, 0xE8, 0x25};
    int funcs = 0;
    funcs += replace_function(img, hf2::kUnlockModeFn, kMode, reinterpret_cast<const void*>(&unlock_mode));
    funcs += replace_function(img, hf2::kUnlockStoryLevelFn, kStory, reinterpret_cast<const void*>(&unlock_story));
    funcs += replace_function(img, hf2::kUnlockMapFn, kMap, reinterpret_cast<const void*>(&unlock_map));
    funcs += replace_function(img, hf2::kUnlockCharFn, kChar, reinterpret_cast<const void*>(&unlock_char));
    funcs += replace_function(img, hf2::kUnlockChallengeFn, kChallenge,
                              reinterpret_cast<const void*>(&unlock_challenge));

    g_file = file;
    g_api.create = reinterpret_cast<CreateFn>(
        hook_call(img, hf2::kProfileCreateCall, hf2::kProfileCreate, reinterpret_cast<const void*>(&create_hook)));
    int ends = 0;
    for (uint64_t va : hf2::kLevelEndCalls) {
        void* orig = hook_call(img, va, hf2::kLevelEnd, reinterpret_cast<const void*>(&level_end_hook));
        if (orig) g_api.level_end = reinterpret_cast<LevelEndFn>(orig), ++ends;
    }
    std::atexit([] { profile_save(); });
    log("[fortschritt] Xbox-Regeln: %d/5 Abfragen, %d/8 Aufrufe; Profil %ls (%s), Levelende %d/3\n", funcs, sites,
        file.c_str(), g_api.create ? "laden/speichern" : "Anlegen nicht gefunden", ends);
}

}  // namespace host
