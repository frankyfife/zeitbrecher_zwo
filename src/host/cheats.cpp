// Cheats aus ts2.ini [cheats] (docs/host.md, "Cheats"). Beide wirken nur für
// die Spieler (Einträge der Spielertabelle), nicht für die KI: die Waffen-
// tabelle und TS2s eigener Cheat "Infinite Ammo" gelten für alle Schützen,
// deshalb ändert hier nichts die Tabelle.
//
// munition=1: Der Verbrauch 0x1411213D8 (beide Aufrufe) wird für Spieler
// übersprungen, Magazin und Vorrat bleiben stehen. TS2s eigener Cheat
// (Optionen → Cheats, Bit 4 von 0x143F2F998) macht an derselben Stelle
// dasselbe, aber für jeden Schützen und nur, wenn der Cheat freigeschaltet
// und in der Spielart erlaubt ist; er bleibt davon unberührt.
//
// kein_cooldown=1: vor jedem Takt
//  - Schussabstand: die Takte seit dem letzten Schuss jeder Hand (+0xB8/+0xBC)
//    stehen mindestens auf dem größten Abstand der Waffe; der Handautomat
//    feuert so in jedem Takt, in dem Feuer gedrückt oder gehalten ist (auch
//    der Anlauf der Minigun, deren erster Abstand länger ist, entfällt).
//  - Nachladen: jedes Magazin wird sofort aus dem Vorrat aufgefüllt (wie
//    0x141121A70 am Ende des Nachladens, auch direkt nach jedem Schuss), es
//    wird also nie leer und TS2 beginnt kein Nachladen. Der Vorrat nimmt ab
//    wie beim Nachladen; ohne Vorrat bleibt das Magazin leer.
//  - Waffenwechsel: Senken und Heben (Zustand 2 und 6 der Waffe) springen
//    auf das Ende; ein Wechsel dauert ein paar Takte statt 0,7 s.
#include "cheats.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

#include "hf2.h"
#include "hooks.h"
#include "log.h"

namespace host {

CheatConfig load_cheat_config(const std::filesystem::path& ini) {
    const std::wstring p = ini.wstring();
    CheatConfig c;
    c.infinite_ammo = GetPrivateProfileIntW(L"cheats", L"munition", 0, p.c_str()) != 0;
    c.no_cooldown = GetPrivateProfileIntW(L"cheats", L"kein_cooldown", 0, p.c_str()) != 0;
    return c;
}

namespace {

const Ts2Image* g_img = nullptr;
CheatConfig g_cfg;
int g_tick = 0;
bool g_probe = false;  // TS2_WAFFENPROBE=1: Verbrauch, Magazin und Waffenzustand von Spieler 1

template <class T>
T& at(uint8_t* p, uint32_t off) {
    return *reinterpret_cast<T*>(p + off);
}

// Spieler i (Eintrag der Spielertabelle) oder nullptr
uint8_t* player(int i) {
    uint8_t* table = *g_img->va<uint8_t*>(hf2::kPlayerTablePtr);
    if (!table || i >= *g_img->va<int32_t>(hf2::kPlayerCount)) return nullptr;
    return table + size_t(i) * 0x12A0;
}

uint8_t* character_of(uint8_t* p) {
    uint8_t* sub = p ? at<uint8_t*>(p, hf2::kPlayerSub) : nullptr;
    return sub ? at<uint8_t*>(sub, hf2::kCharSub) : nullptr;
}

bool is_player_character(uint8_t* chr) {
    if (!chr) return false;
    for (int i = 0; i < 4; ++i) {
        uint8_t* p = player(i);
        if (!p) break;
        if (character_of(p) == chr) return true;
    }
    return false;
}

uint8_t* weapon_row(uint8_t* hand) {
    const int32_t row = at<int32_t>(hand, hf2::kHandRow);
    if (row < 0 || row >= hf2::kWeaponRows) return nullptr;
    return g_img->va(hf2::kWeaponTable) + size_t(row) * hf2::kWeaponRowSize;
}

// Magazin aus dem Vorrat auffüllen, wie 0x141121A70 (ohne dessen Rücksetzen
// der Schusszähler): Magazingröße 0 oder 1 heißt ohne Magazin.
void top_up(uint8_t* chr, uint8_t* hand) {
    uint8_t* row = weapon_row(hand);
    if (!row) return;
    const int32_t size = at<int32_t>(row, hf2::kRowClipSize);
    auto fill = [&](int32_t type) {
        if (type < 0 || type >= 32) return;
        int32_t& pool = at<int32_t>(chr, hf2::kCharAmmoPool + 4 * type);
        int32_t& clip = at<int32_t>(hand, hf2::kHandClip + 4 * type);
        const int32_t n = std::min(size - clip, pool);
        if (n <= 0) return;
        clip += n;
        pool -= n;
    };
    const int32_t type = at<int32_t>(row, hf2::kRowAmmoType);
    if (size != 0 && size != 1) fill(type);
    const int32_t type2 = at<int32_t>(row, hf2::kRowAmmoType2), clip2 = at<int32_t>(row, hf2::kRowClip2);
    if (type2 != 0 && type2 != type && clip2 != 0 && clip2 != 1) fill(type2);
}

// Verbrauch (0x1411213D8)
using ConsumeFn = void (*)(uint8_t* chr, uint8_t* hand, int type, int amount, int mode);
ConsumeFn g_consume = nullptr;

void consume_hook(uint8_t* chr, uint8_t* hand, int type, int amount, int mode) {
    const bool own = is_player_character(chr);
    if (g_probe && own) {
        log("[waffe] Takt %d: Schuss Hand +0x%llX, Art %d, Menge %d%s\n", g_tick,
            static_cast<unsigned long long>(hand - chr), type, amount,
            g_cfg.infinite_ammo ? " (Cheat: nichts verbraucht)" : "");
    }
    if (own && g_cfg.infinite_ammo) return;
    g_consume(chr, hand, type, amount, mode);
    // gleich nachfüllen: der Handautomat prüft das Magazin noch in diesem Takt
    if (own && g_cfg.no_cooldown && hand) top_up(chr, hand);
}

}  // namespace

void cheats_install(const Ts2Image& img, const CheatConfig& c) {
    g_img = &img;
    g_cfg = c;
    g_probe = GetEnvironmentVariableW(L"TS2_WAFFENPROBE", nullptr, 0) > 0;
    if (c.infinite_ammo || c.no_cooldown || g_probe) {
        const auto fn = reinterpret_cast<const void*>(&consume_hook);
        auto* a = hook_call(img, hf2::kAmmoConsumeCallShot, hf2::kAmmoConsume, fn);
        auto* b = hook_call(img, hf2::kAmmoConsumeCallOther, hf2::kAmmoConsume, fn);
        g_consume = reinterpret_cast<ConsumeFn>(a ? a : b);
        if (!a || !b) log("Cheats: Verbrauch 0x1411213D8 nicht an beiden Aufrufen gefunden\n");
    }
    if (c.infinite_ammo || c.no_cooldown)
        log("Cheats: Munition %s, kein Cooldown %s (nur Spieler)\n", c.infinite_ammo ? "an" : "aus",
            c.no_cooldown ? "an" : "aus");
}

void cheats_tick(const Ts2Image& img) {
    (void)img;
    ++g_tick;
    if (!g_img || (!g_cfg.no_cooldown && !g_probe)) return;
    for (int i = 0; i < 4; ++i) {
        uint8_t* p = player(i);
        uint8_t* chr = character_of(p);
        if (!chr) break;
        for (int h = 0; h < 2; ++h) {
            uint8_t* hand = chr + hf2::kCharHands[h];
            uint8_t* gun = p + hf2::kPlayerGuns[h];
            uint8_t* row = weapon_row(hand);
            if (g_probe && i == 0 && row) {
                static int32_t last[2][3] = {{-1, -1, -1}, {-1, -1, -1}};
                const int32_t type = at<int32_t>(row, hf2::kRowAmmoType);
                const int32_t now[3] = {at<int32_t>(gun, hf2::kGunState),
                                        type >= 0 && type < 32 ? at<int32_t>(hand, hf2::kHandClip + 4 * type) : -1,
                                        type >= 0 && type < 32 ? at<int32_t>(chr, hf2::kCharAmmoPool + 4 * type) : -1};
                if (std::memcmp(now, last[h], sizeof now) != 0) {
                    log("[waffe] Takt %d: Hand %d Zeile %d: Zustand %d, Magazin %d, Vorrat %d\n", g_tick, h,
                        at<int32_t>(hand, hf2::kHandRow), now[0], now[1], now[2]);
                    std::memcpy(last[h], now, sizeof now);
                }
            }
            if (!g_cfg.no_cooldown) continue;
            if (row) {
                // Schussabstand: Takte seit dem Schuss mindestens auf dem größten Abstand
                int32_t need = 0;
                for (int m = 0; m < 2; ++m)
                    for (uint32_t k = 0x10; k <= 0x14; k += 4) {
                        const int32_t d = at<int32_t>(row, 0x58 + m * 0xB0 + k);
                        if (d > 0 && d <= 10000) need = std::max(need, d);
                    }
                for (int m = 0; m < 2; ++m) {
                    int32_t& since = at<int32_t>(hand, hf2::kHandSinceShot + 4 * m);
                    if (since < need) since = need;
                }
                top_up(chr, hand);
            }
            // Waffenwechsel: Senken (2) und Heben (6) sofort zu Ende
            const int32_t state = at<int32_t>(gun, hf2::kGunState);
            if (state == 2 || state == 6) at<float>(gun, hf2::kGunProgress) = 1.0f;
        }
    }
}

}  // namespace host
