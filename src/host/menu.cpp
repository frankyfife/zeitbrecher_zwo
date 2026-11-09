#include "menu.h"

#include <windows.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <fstream>

#include "hf2.h"
#include "log.h"

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

// Hauptmenü: Story, Arcade, Challenge (je nach Flag), Options, Quit.
// Maße, Texte und Ablauf wie die Aufbau-Funktion der Homefront-Fassung.
void build_main_menu(uint8_t* page) {
    *reinterpret_cast<uint64_t*>(page + 0x650) = 0;  // kein Titel
    *reinterpret_cast<int32_t*>(page + 0x660) = 0xB;
    *reinterpret_cast<int32_t*>(page + 0x658) = 0xA8;
    *reinterpret_cast<int32_t*>(page + 0x65C) = 0x360;
    uint8_t* menu = g_api.create(page, 0x294);

    const int32_t players = *g_api.players;
    uint8_t* story = add_entry(menu, text(players == 2 ? hf2::kStrStoryCoop : hf2::kStrStory), g_api.story);
    if (players > 2) disable(story);
    if (g_api.flags[hf2::kUnlockArcade]) add_entry(menu, text(hf2::kStrArcade), g_api.arcade);
    if (g_api.flags[hf2::kUnlockChallenge]) {
        uint8_t* challenge = add_entry(menu, text(hf2::kStrChallenge), g_api.challenge);
        if (players > 1) disable(challenge);
    }
    add_entry(menu, text(hf2::kStrOptions), goto_options);
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
    int replaced = 0;
    size_t pos = 0;
    for (int i = 0; i < kEntries; ++i) {
        const char* s = pos < blob.size() ? blob.c_str() + pos : "";
        const size_t len = pos < blob.size() ? std::strlen(s) : 0;
        pos += len + 1;
        const auto* pv = reinterpret_cast<const uint8_t*>(pc[i]);
        table[i] = (pv >= img.base && pv < img.base + img.size) ? pc[i] : "";  // nur gültige PC-Zeiger
        if (len) table[i] = s, ++replaced;
    }
    *const_cast<const char* const**>(pc_slot) = table.data();
    log("Sprache %ls: %d von %d Texten\n", code.c_str(), replaced, kEntries);
}

void unlock_install(const Ts2Image& img, const UnlockConfig& c) {
    char* flags = img.va<char>(hf2::kUnlockFlags);
    flags[hf2::kUnlockStory] = c.story;
    flags[hf2::kUnlockArcade] = c.arcade;
    flags[hf2::kUnlockChallenge] = c.challenge;
    flags[hf2::kUnlockCheats] = c.cheats;
    flags[hf2::kUnlockInvincible] = c.invincible;
    *img.va<int32_t>(hf2::kUnlockAll) = c.all;
    log("Freischaltungen: story=%d arcade=%d challenge=%d cheats=%d unverwundbar=%d alles=%d\n", c.story,
        c.arcade, c.challenge, c.cheats, c.invincible, c.all);

    if (!c.options_entry) return;
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

    void** slot = reinterpret_cast<void**>(img.va(hf2::kMainMenuPage + hf2::kPageBuild));
    if (*slot != img.va<void>(hf2::kMainMenuBuild)) {
        log("Hauptmenü: unerwarteter Aufbau %p, nicht ersetzt\n", *slot);
        return;
    }
    *slot = reinterpret_cast<void*>(&build_main_menu);
    log("Hauptmenü mit \"Options\"\n");
}

}  // namespace host
