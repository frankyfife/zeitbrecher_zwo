#include "menu.h"

#include <windows.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <fstream>

#include "hf2.h"
#include "hooks.h"
#include "log.h"
#include "multiplayer.h"
#include "profile.h"

namespace host {

UnlockConfig load_unlock_config(const std::filesystem::path& ini) {
    const std::wstring p = ini.wstring();
    auto get = [&](const wchar_t* key, bool def) {
        return GetPrivateProfileIntW(L"freischalten", key, def ? 1 : 0, p.c_str()) != 0;
    };
    UnlockConfig c;
    c.story = get(L"story", c.story);
    c.arcade = get(L"arcade", c.arcade);
    c.challenge = get(L"challenge", c.challenge);
    c.characters = get(L"figuren", c.characters);
    c.cheats = get(L"cheats", c.cheats);
    c.invincible = get(L"unverwundbar", c.invincible);
    c.all = get(L"alles", c.all);
    c.options_entry = get(L"optionen", c.options_entry);
    return c;
}

namespace {

// Bausteine des TS2-Menüsystems (Adressen in hf2.h)
using MenuCreateFn = uint8_t* (*)(uint8_t* page, int height);
using MenuAddEntryFn = uint8_t* (*)(uint8_t* menu, int, int);
using MenuIntFn = int (*)(uint8_t*);
using MenuVoidFn = void (*)(uint8_t*);
using MenuVoidIntFn = void (*)(uint8_t*, int);
using SetNextPageFn = void (*)(void*);
using ActionFn = void (*)();

struct MenuApi {
    MenuCreateFn create;
    MenuAddEntryFn add_entry;
    MenuIntFn entry_width, menu_x, menu_y;
    MenuVoidFn layout, finish;
    MenuVoidIntFn select;
    MenuVoidIntFn page_show;
    SetNextPageFn set_next_page;
    ActionFn story, arcade, challenge, quit;
    const float* y_offset;
    const float* hint_offset;
    uint8_t* const* string_tables;
    const int32_t* language;
    const int32_t* players;
    const char* flags;
    void* options_page;
    const int32_t* main_y_imm;  // imm32 in der Original-Aufbaufunktion
    const int32_t* main_h_imm;
    bool options_entry;
} g_api;

const char* text(uint32_t id) {
    const uint8_t* table = g_api.string_tables[*g_api.language];
    return *reinterpret_cast<const char* const*>(table + id);
}

// Ein Eintrag wie im Original: Schrift 0x15, Ausrichtung 3, Abstand 5.
uint8_t* add_entry(uint8_t* menu, const char* label, ActionFn action) {
    uint8_t* e = g_api.add_entry(menu, 0x49, 0x100);
    *reinterpret_cast<int32_t*>(e + 0x30) = 0x15;
    *reinterpret_cast<const char**>(e + 0x38) = label;
    *reinterpret_cast<int32_t*>(e + 0x40) = 3;
    *reinterpret_cast<int32_t*>(e + 0x58) = g_api.entry_width(e);
    *reinterpret_cast<int32_t*>(e + 0x5C) = 5;
    *reinterpret_cast<ActionFn*>(e + 0x50) = action;
    return e;
}

void disable(uint8_t* entry) { *reinterpret_cast<uint32_t*>(entry + 0x44) |= 1; }

void goto_options() { g_api.set_next_page(g_api.options_page); }

// Hauptmenü wie auf der Xbox: Story, Arcade, Challenge (immer), Options
// (laut [freischalten] optionen), Quit. Maße, Texte und Ablauf wie die
// Aufbau-Funktion der Homefront-Fassung. Zurück im Hauptmenü wird das
// Profil gespeichert (wie die Konsole nach dem Level/den Optionen).
void build_main_menu(uint8_t* page) {
    profile_save();
    *reinterpret_cast<uint64_t*>(page + 0x650) = 0;  // kein Titel
    *reinterpret_cast<int32_t*>(page + 0x660) = 0xB;
    // Werte aus den Befehlen der Original-Aufbaufunktion (mov dword [rcx+0x658],
    // imm32 bei 0x1410C81E6 usw.): 0xA8/0x360, mit TS2 Redux 0/0x3E0
    // (Hauptmenü ohne Balken, wie Redux es auslegt).
    *reinterpret_cast<int32_t*>(page + 0x658) = *g_api.main_y_imm;
    *reinterpret_cast<int32_t*>(page + 0x65C) = *g_api.main_h_imm;
    uint8_t* menu = g_api.create(page, 0x294);

    const int32_t players = *g_api.players;
    uint8_t* story = add_entry(menu, text(players == 2 ? hf2::kStrStoryCoop : hf2::kStrStory), g_api.story);
    if (players > 2) disable(story);
    add_entry(menu, text(hf2::kStrArcade), g_api.arcade);
    uint8_t* challenge = add_entry(menu, text(hf2::kStrChallenge), g_api.challenge);
    if (players > 1) disable(challenge);
    if (g_api.options_entry) add_entry(menu, text(hf2::kStrOptions), goto_options);
    add_entry(menu, text(hf2::kStrQuit), g_api.quit);

    const int32_t first = *reinterpret_cast<int32_t*>(menu + 0x1C);
    *reinterpret_cast<int32_t*>(menu + 0x2C) = 0;
    *reinterpret_cast<int32_t*>(menu + 0x28) = first;
    *reinterpret_cast<int32_t*>(menu + 0x30) = first;
    *reinterpret_cast<int32_t*>(menu + 0x34) = 0x5A;
    *reinterpret_cast<int32_t*>(menu + 0x38) = 0x36;
    *reinterpret_cast<float*>(menu + 0x20) = float(g_api.menu_x(menu));
    *reinterpret_cast<float*>(menu + 0x64) = 0.2f;
    *reinterpret_cast<float*>(menu + 0x24) = float(g_api.menu_y(menu)) + *g_api.y_offset;
    strcpy_s(reinterpret_cast<char*>(menu + 0x19C), 0x80, text(hf2::kStrSelectHint));
    strcpy_s(reinterpret_cast<char*>(menu + 0x21C), 0x80, text(hf2::kStrBackHint));
    *reinterpret_cast<int32_t*>(menu + 0x198) = 2;
    *reinterpret_cast<int32_t*>(menu + 0x194) =
        int(float(*reinterpret_cast<int32_t*>(page + 0x65C)) + *g_api.hint_offset);
    g_api.layout(menu);
    g_api.select(menu, 0);
    g_api.finish(menu);
    *reinterpret_cast<uint32_t*>(page + 0x10) |= 0x10;
    g_api.page_show(page, 0);
}

}  // namespace

// Die Xbox-PAL-Fassung hat fünf Texttabellen mit je 2770 Einträgen in
// derselben Reihenfolge wie die eine Tabelle des PC-Ports (2386 Einträge
// wörtlich gleich, der Rest britische Schreibweise oder Plattformwörter).
// Die gewählte Sprache ersetzt den Zeiger auf die PC-Tabelle
// (0x1424E2430, Sprache 0); leere Einträge behalten den PC-Text.
void language_install(const Ts2Image& img, const std::filesystem::path& game, const std::wstring& code) {
    if (code.empty() || code == L"pc") return;
    const auto file = game / L"sprache" / (code + L".bin");
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        log("Sprache %ls: %ls fehlt (tools/re/xbox_text.py)\n", code.c_str(), file.c_str());
        return;
    }
    static std::string blob;
    blob.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    constexpr int kEntries = 2770;
    static std::vector<const char*> table(kEntries, nullptr);
    auto** pc_slot = img.va<const char* const*>(hf2::kStringTables);
    const char* const* pc = *pc_slot;
    // Xbox-Einträge der Reihe nach
    std::vector<const char*> xbox(kEntries, "");
    size_t pos = 0;
    for (int i = 0; i < kEntries && pos < blob.size(); ++i) {
        xbox[i] = blob.c_str() + pos;
        pos += std::strlen(xbox[i]) + 1;
    }
    // Die Xbox-Tabelle hat bei 2425 einen Eintrag mehr (zweite Fassung von
    // "A Save game of that name already exists"); ab dort ist PC-Eintrag i
    // der Xbox-Eintrag i+1 (geprüft mit der englischen Xbox-Tabelle: bis 2424
    // 2369 gleiche Texte ohne Versatz, danach 298 von 327 mit Versatz 1,
    // keiner ohne). Ohne das stand am Container in Siberia "%d/%d Spieler".
    constexpr int kXboxExtra = 2425;
    int replaced = 0;
    for (int i = 0; i < kEntries; ++i) {
        const int x = i >= kXboxExtra ? i + 1 : i;
        const char* s = x < kEntries ? xbox[x] : "";
        const auto* pv = reinterpret_cast<const uint8_t*>(pc[i]);
        table[i] = (pv >= img.base && pv < img.base + img.size) ? pc[i] : "";  // nur gültige PC-Zeiger
        if (*s && *table[i]) table[i] = s, ++replaced;  // im PC-Port leere Einträge bleiben leer
    }
    *const_cast<const char* const**>(pc_slot) = table.data();
    log("Sprache %ls: %d von %d Texten\n", code.c_str(), replaced, kEntries);
}

// 3D-Menüs (Pause, "Beenden?", Tod, Levelende): Die Texte zeichnet
// 0x1410DAA74 über 0x14121DA7C an (x, v) mit v = Eintrag +0x98; die Balken
// liegen anders. Am Bild gemessen (1920×1080, Pausenmenü Siberia): Texte
// y = 571 − 593·v, Balkenmitten y ≈ 562,6 − 739·v. Die Texte saßen dadurch
// unter bzw. neben ihren Balken. v wird für die Textausgabe so umgerechnet,
// dass die Texte auf den Balkenmitten liegen: v' = 0,0142 + 1,2462·v.
using Text3dFn = void (*)(void*, float*, void*, void*, void*, void*);
Text3dFn g_text3d = nullptr;
void text3d_hook(void* a, float* pos, void* c, void* d, void* e, void* f) {
    // Mehrspieler: 2D liegt unverstaucht in Bildschirmpixeln
    // (multiplayer.cpp), Texte und Balken liegen ohne Umrechnung aufeinander
    if (pos && !mp_active()) {
        float moved[2] = {pos[0], 0.0142f + 1.2462f * pos[1]};
        g_text3d(a, moved, c, d, e, f);
        return;
    }
    g_text3d(a, pos, c, d, e, f);
}

void unlock_install(const Ts2Image& img, const UnlockConfig& c) {
    g_text3d = reinterpret_cast<Text3dFn>(
        hook_call(img, hf2::kMenuText3dCall, hf2::kMenuText3d, reinterpret_cast<const void*>(&text3d_hook)));
    if (!g_text3d) log("3D-Menütexte: Aufruf 0x1410DAC81 nicht gefunden\n");
    char* flags = img.va<char>(hf2::kUnlockFlags);
    flags[hf2::kUnlockStory] = c.story;
    flags[hf2::kUnlockArcade] = c.arcade;
    flags[hf2::kUnlockChallenge] = c.challenge;
    flags[hf2::kUnlockChars] = c.characters;
    flags[hf2::kUnlockCheats] = c.cheats;
    flags[hf2::kUnlockInvincible] = c.invincible;
    *img.va<int32_t>(hf2::kUnlockAll) = c.all;
    log("Freischaltungen: story=%d arcade=%d challenge=%d figuren=%d cheats=%d unverwundbar=%d alles=%d\n",
        c.story, c.arcade, c.challenge, c.characters, c.cheats, c.invincible, c.all);

    // Das eigene Hauptmenü zeigt Arcade und Challenge immer (wie die Xbox);
    // die Flags schalten nur noch alles darin frei
    g_api.options_entry = c.options_entry;
    auto fn = [&](uint64_t va) { return img.va<void>(va); };
    g_api.create = reinterpret_cast<MenuCreateFn>(fn(hf2::kMenuCreate));
    g_api.add_entry = reinterpret_cast<MenuAddEntryFn>(fn(hf2::kMenuAddEntry));
    g_api.entry_width = reinterpret_cast<MenuIntFn>(fn(hf2::kEntryWidth));
    g_api.menu_x = reinterpret_cast<MenuIntFn>(fn(hf2::kMenuX));
    g_api.menu_y = reinterpret_cast<MenuIntFn>(fn(hf2::kMenuY));
    g_api.layout = reinterpret_cast<MenuVoidFn>(fn(hf2::kMenuLayout));
    g_api.finish = reinterpret_cast<MenuVoidFn>(fn(hf2::kMenuFinish));
    g_api.select = reinterpret_cast<MenuVoidIntFn>(fn(hf2::kMenuSelect));
    g_api.page_show = reinterpret_cast<MenuVoidIntFn>(fn(hf2::kPageShow));
    g_api.set_next_page = reinterpret_cast<SetNextPageFn>(fn(hf2::kSetNextPage));
    g_api.story = reinterpret_cast<ActionFn>(fn(hf2::kGotoStory));
    g_api.arcade = reinterpret_cast<ActionFn>(fn(hf2::kGotoArcade));
    g_api.challenge = reinterpret_cast<ActionFn>(fn(hf2::kGotoChallenge));
    g_api.quit = reinterpret_cast<ActionFn>(fn(hf2::kQuit));
    g_api.y_offset = img.va<float>(hf2::kMenuYOffset);
    g_api.hint_offset = img.va<float>(hf2::kMenuHintOffset);
    g_api.string_tables = img.va<uint8_t* const>(hf2::kStringTables);
    g_api.language = img.va<int32_t>(hf2::kLanguage);
    g_api.players = img.va<int32_t>(hf2::kPlayersInGame);
    g_api.flags = flags;
    g_api.options_page = img.va<void>(hf2::kOptionsPage);
    g_api.main_y_imm = img.va<const int32_t>(hf2::kMainMenuBuild + 0x34);  // 0x1410C81EC
    g_api.main_h_imm = img.va<const int32_t>(hf2::kMainMenuBuild + 0x3E);  // 0x1410C81F6

    void** slot = reinterpret_cast<void**>(img.va(hf2::kMainMenuPage + hf2::kPageBuild));
    if (*slot != img.va<void>(hf2::kMainMenuBuild)) {
        log("Hauptmenü: unerwarteter Aufbau %p, nicht ersetzt\n", *slot);
        return;
    }
    *slot = reinterpret_cast<void*>(&build_main_menu);
    log("Hauptmenü wie auf der Xbox%s\n", c.options_entry ? ", mit \"Options\"" : "");
}

}  // namespace host
