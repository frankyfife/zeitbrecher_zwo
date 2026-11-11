#include "multiplayer.h"

#include <windows.h>
#include <d3d11_1.h>
#include <xinput.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "hf2.h"
#include "hooks.h"
#include "input.h"
#include "log.h"

namespace host {

namespace {

// ---- Adressen (docs/host.md "Mehrspieler")
// Pads: 8 Pad-Daten je 0x60 Byte nach Port (PS2: Multitap), gelesen je Takt
// von 0x1411458F4 mit scePadRead(port, pad). Spieler i nimmt das Pad
// kPadMap[i] (7 = keins); die Zuordnung kopiert 0x1411458F4 je Takt aus der
// Port-Suche 0x141225878, die im PC-Port nur Eintrag 0 setzt (0 Pad, 6 Maus,
// 7 keins). scePadRead füllt nur diesen einen Port.
constexpr uint64_t kPads = 0x142515350;
constexpr uint32_t kPadStride = 0x60;
constexpr uint64_t kPadRead = 0x141225898, kPadReadCall = 0x1411459E7;
constexpr uint64_t kPadScan = 0x141225878, kPadScanCall = 0x14114598B;
// Pad-Daten: +0x14 0x63 = verbunden, +0x20 Knöpfe vorher, +0x24 jetzt,
// +0x28 gedrückt, +0x2C losgelassen, +0x30 links x, +0x34 rechts x,
// +0x38 −links y, +0x3C −rechts y, +0x44/+0x45 Rüttelmotoren (0..127)
// Seite "nplayers_page" (+0 Takt, +0x20 Aufbau): in der Homefront-Fassung
// setzt der Takt 1 Spieler und geht ins Hauptmenü ("gametype_page").
constexpr uint64_t kNPlayersPage = 0x1424F2E20;
constexpr uint64_t kNPlayersTick = 0x1410C8FDC, kNPlayersBuild = 0x1410C8FD4;
constexpr uint64_t kTickCountImm = 0x1410C8FFA;    // B9 01000000: mov ecx, 1 (lokale Spieler)
constexpr uint64_t kTickPlayersImm = 0x1410C9004;  // C7 05 … 01000000: Spieleranzahl = 1
constexpr uint64_t kPageInput = 0x1410DC344, kPageUpdate = 0x1410E0334;  // void (page*)
// Zeichnen (+8) wie das Hauptmenü: Hintergrund, Logo und Titel, Menüs; die
// Seite der Homefront-Fassung hat keins (sie bleibt nur einen Takt stehen)
constexpr uint64_t kMenuPageDraw = 0x1410C1E9C;
// Zurück aus dem Hauptmenü ("gametype_page", Takt +0): die Homefront-Fassung
// kommt nicht mehr zur Spieleranzahl; Kreis (B) führt wie auf der Xbox
// dorthin, wenn mindestens zwei Pads stecken oder schon mehrere spielen.
constexpr uint64_t kMainMenuTick = 0x1410D134C;
constexpr uint64_t kMenuButtons = 0x1410B4344;   // int (Menüspieler): neu gedrückte Tasten
constexpr uint64_t kPageLeave = 0x1410B4708;     // void (page*)
constexpr uint64_t kPageGoto = 0x1410B4854;      // void* (Seitenbeschreibung*)
// imm32 der Seitenmaße im Aufbau von "Arcade Type" (mov dword [rcx+0x658/0x65C/0x660], imm)
constexpr uint64_t kArcadeTypeY = 0x1410C0DC3, kArcadeTypeH = 0x1410C0DCD, kArcadeTypeHead = 0x1410C0DE3;
// 0x14106A01C legt die Profile an: mit einem Spieler eins (Name aus Windows,
// profile.cpp lädt profil.bin hinein), sonst je Spieler eins ("%s %c").
constexpr uint64_t kProfileCreate = 0x14110E7C4;
constexpr uint64_t kProfileCreateCall1 = 0x14106A0D8, kProfileCreateCallN = 0x14106A117;
// Texte (Offset in der Texttabelle): "1 Player" … "4 Players", "Player 2" …
constexpr uint32_t kStrNPlayers = 0xE30, kStrPlayerN = 0x7F8, kStrSelectPlayers = 0x4220;

using PadReadFn = void (*)(int port, uint8_t* pad);
using PadScanFn = void (*)(int32_t* table, int n);
using PageFn = void (*)(uint8_t* page);
using CreateFn = int (*)(const char* name);
using MenuCreateFn = uint8_t* (*)(uint8_t* page, int height);
using MenuAddEntryFn = uint8_t* (*)(uint8_t* menu, int, int);
using MenuIntFn = int (*)(uint8_t*);
using MenuVoidFn = void (*)(uint8_t*);
using MenuVoidIntFn = void (*)(uint8_t*, int);
using ActionFn = void (*)();
using WalkFn = void (*)(void* ctx, float* a, float step1, float* b, float* c, float step2);
using ClearFn = void (*)(int x0, int y0, int x1, int y1, uint32_t rgba);

// Löschen 0x14122A4FC(x0, y0, x1, y1, 0xRRGGBBAA): auf PS2/Xbox nur das
// Rechteck (Bildausschnitt der Kamera); der PC-Port löscht mit
// ClearRenderTargetView das ganze Ziel (RTV 0x143020AD8) samt Tiefe
// (DSV 0x142FE5930). Mit zwei Ansichten löschte die zweite so die erste.
constexpr uint64_t kClear = 0x14122A4FC;
constexpr uint64_t kClearCalls[] = {0x14107C1BD, 0x14107C257, 0x14107C30F, 0x14107F410,
                                    0x14107F589, 0x1410B584D, 0x1410B5A1E};
constexpr uint64_t kDeviceHolder = 0x143020AC8, kRtv = 0x143020AD8, kDsv = 0x142FE5930;

struct Api {
    PadReadFn pad_read = nullptr;
    PadScanFn pad_scan = nullptr;
    PageFn tick = nullptr, build = nullptr, page_input = nullptr, page_update = nullptr;
    PageFn main_tick = nullptr, page_leave = nullptr;
    int (*menu_buttons)(int) = nullptr;
    void* (*page_goto)(void*) = nullptr;
    CreateFn create = nullptr, create_p1 = nullptr;
    MenuCreateFn menu_create;
    MenuAddEntryFn add_entry;
    MenuIntFn entry_width, menu_x, menu_y;
    MenuVoidFn layout, finish;
    MenuVoidIntFn select, page_show;
    WalkFn walk = nullptr;
    ClearFn clear = nullptr;
    const float* y_offset;
    const float* hint_offset;
    uint8_t* const* string_tables;
    const int32_t* language;
    int32_t* fe_players;      // 0x143F35188, Spieler im Menü
} g_api;
const Ts2Image* g_img = nullptr;
void** g_desc = nullptr;      // Beschreibung der Seite "nplayers_page"
void* g_desc_draw = nullptr;  // ihr Zeichnen (+8) im Original

bool g_enabled = true;      // [mehrspieler] an
int g_players = 1;          // gewählt (1 = wie bisher)
int g_choice = 0;           // im Menü gewählt, noch nicht übernommen
bool g_menu = false;        // Seite zeigt die Auswahl
bool g_applied = false;     // Wahl in diesem Besuch der Seite übernommen
int g_create_n = 0;         // Profile im Mehrspieler-Zweig
bool g_probe = false;       // TS2_MPPROBE: Knöpfe der Spieler 2–4, Lage aller Spieler

// Bewegung wie für Spieler 1 (features.cpp): Sprung und Rennen
bool g_jump = true, g_run = true, g_run_toggle = false;
float g_jump_height = 1.2f, g_run_factor = 1.6f;

const char* text(uint32_t id) {
    const uint8_t* table = g_api.string_tables[*g_api.language];
    return *reinterpret_cast<const char* const*>(table + id);
}

// ---- Pads der Spieler 2–4
struct Slot {
    int user = -1;             // XInput-Nummer, −1 keins
    bool present = false;
    PadOutput out;             // übersetzt, für TS2 (Port = Spieler − 1)
    uint32_t prev = 0;         // Knöpfe des vorigen Lesens
    WORD motor_set[2] = {}, motor_want[2] = {};
    bool motor_valid = false;
    bool run_latched = false, run_was = false, running = false;
    bool jump_was = false;
    float last[3] = {};
    bool have_last = false;
};
Slot g_slot[kMaxPads];

// Testskript TS2_MPSKRIPT="spieler:takt:knoepfe,lx,ly,rx,ry,lt,rt[,ts2bits];…"
// (spieler 2–4, sonst wie TS2_PADSKRIPT); ersetzt XInput für diese Spieler.
struct ScriptStep {
    int player;
    uint32_t tick;
    XINPUT_GAMEPAD g;
    uint32_t raw;
};
std::vector<ScriptStep> g_script;
int g_script_players = 0;  // höchster Spieler im Skript

void script_load() {
    wchar_t buf[8192] = {};
    if (!GetEnvironmentVariableW(L"TS2_MPSKRIPT", buf, 8192)) return;
    std::wstring s = buf;
    size_t i = 0;
    while (i < s.size()) {
        size_t e = s.find(L';', i);
        std::wstring part = s.substr(i, e == std::wstring::npos ? std::wstring::npos : e - i);
        ScriptStep st{};
        unsigned b = 0, raw = 0;
        int lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
        if (swscanf(part.c_str(), L"%d:%u:%x,%d,%d,%d,%d,%d,%d,%x", &st.player, &st.tick, &b, &lx, &ly, &rx, &ry, &lt,
                    &rt, &raw) >= 3 &&
            st.player >= 2 && st.player <= kMaxPads) {
            st.g.wButtons = WORD(b);
            st.g.sThumbLX = SHORT(std::clamp(lx, -32768, 32767));
            st.g.sThumbLY = SHORT(std::clamp(ly, -32768, 32767));
            st.g.sThumbRX = SHORT(std::clamp(rx, -32768, 32767));
            st.g.sThumbRY = SHORT(std::clamp(ry, -32768, 32767));
            st.g.bLeftTrigger = BYTE(std::clamp(lt, 0, 255));
            st.g.bRightTrigger = BYTE(std::clamp(rt, 0, 255));
            st.raw = raw;
            g_script.push_back(st);
            g_script_players = std::max(g_script_players, st.player);
        }
        if (e == std::wstring::npos) break;
        i = e + 1;
    }
    log("[mehrspieler] Skript: %zu Einträge, Spieler bis %d\n", g_script.size(), g_script_players);
}

// Angeschlossene XInput-Pads
bool scripted() { return !g_script.empty() || input_pad_scripted(); }

bool connected(DWORD u) {
    XINPUT_STATE xs{};
    return XInputGetState(u, &xs) == ERROR_SUCCESS;
}

// Pads insgesamt (Spieler 1 mitgezählt), für die Auswahl der Spieleranzahl
int pad_count() {
    if (scripted()) return (input_pad1_present() ? 1 : 0) + std::max(0, g_script_players - 1);
    int n = 0;
    for (DWORD u = 0; u < XUSER_MAX_COUNT; ++u) n += connected(u);
    return input_pad_on() ? n : 0;
}

// Bei der Wahl der Spieleranzahl: Spieler 1 behält sein Pad (input.cpp),
// die übrigen bekommen die weiteren angeschlossenen Pads der Reihe nach.
// Die Zuordnung bleibt, bis die Seite wieder gewählt wird; ein gezogenes
// Pad lässt seinen Spieler stehen, bis es wieder steckt.
int g_user[kMaxPads] = {-1, -1, -1, -1};

void freeze_pads() {
    for (int& u : g_user) u = -1;
    if (scripted()) return;
    g_user[0] = input_pad1_user();
    int p = 1;
    for (DWORD u = 0; u < XUSER_MAX_COUNT && p < kMaxPads; ++u)
        if (int(u) != g_user[0] && connected(u)) g_user[p++] = int(u);
    log("[mehrspieler] Pads (XInput): %d %d %d %d\n", g_user[0], g_user[1], g_user[2], g_user[3]);
}

void assign_pads() {
    const bool on = input_focused() && input_pad_on();
    for (int p = 1; p < kMaxPads; ++p) {
        g_slot[p].user = scripted() ? -1 : g_user[p];
        g_slot[p].present = scripted() ? p + 1 <= g_script_players : on && g_user[p] >= 0;
    }
}

bool pad_state(int p, uint32_t frame, XINPUT_GAMEPAD* g, uint32_t* raw) {
    *g = {};
    *raw = 0;
    if (scripted()) {
        const ScriptStep* cur = nullptr;
        for (const auto& st : g_script)
            if (st.player == p + 1 && st.tick <= frame) cur = &st;
        if (cur) *g = cur->g, *raw = cur->raw;
        return g_slot[p].present;
    }
    if (!g_slot[p].present || g_slot[p].user < 0) return false;
    XINPUT_STATE xs{};
    if (XInputGetState(DWORD(g_slot[p].user), &xs) != ERROR_SUCCESS) return false;
    *g = xs.Gamepad;
    return true;
}

void motors_apply(Slot& s, bool active) {
    WORD l = 0, r = 0;
    float strength = 1.0f;
    const bool vib = input_pad_vibration(&strength);
    if (active && vib) l = s.motor_want[0], r = s.motor_want[1];
    if (s.motor_valid && l == s.motor_set[0] && r == s.motor_set[1]) return;
    s.motor_set[0] = l;
    s.motor_set[1] = r;
    s.motor_valid = true;
    if (g_probe) log("[mehrspieler] Takt %u: Motoren Spieler %d links %u rechts %u\n", input_frame_number(),
                     int(&s - g_slot) + 1, l, r);
    if (s.user < 0) return;
    XINPUT_VIBRATION v{l, r};
    XInputSetState(DWORD(s.user), &v);
}

void motors_off_at_exit() {
    for (auto& s : g_slot)
        if (s.user >= 0 && s.motor_valid && (s.motor_set[0] || s.motor_set[1])) {
            XINPUT_VIBRATION v{};
            XInputSetState(DWORD(s.user), &v);
        }
}

// ---- Hüllen um TS2s Pad-Lesen
void pad_scan_hook(int32_t* table, int n) {
    g_api.pad_scan(table, n);
    if (g_players <= 1) return;
    // Spieler i auf Port i; Spieler 1 im Pad-Modus (Port 0, input.cpp)
    for (int i = 0; i < 8; ++i) table[i] = i < g_players ? i : 7;
}

void pad_read_hook(int port, uint8_t* pad) {
    g_api.pad_read(port, pad);
    if (g_players <= 1) return;
    if (port == 0) input_rumble_p1(pad[0x44], pad[0x45]);  // Motoren von Spieler 1
    if (port < 1 || port >= g_players || port >= kMaxPads) return;
    Slot& s = g_slot[port];
    const uint32_t now = s.present ? s.out.bits : 0;
    *reinterpret_cast<uint32_t*>(pad + 0x14) = 0x63;
    *reinterpret_cast<uint32_t*>(pad + 0x20) = s.prev;
    *reinterpret_cast<uint32_t*>(pad + 0x24) = now;
    *reinterpret_cast<uint32_t*>(pad + 0x28) = now & ~s.prev;
    *reinterpret_cast<uint32_t*>(pad + 0x2C) = s.prev & ~now;
    *reinterpret_cast<float*>(pad + 0x30) = s.present ? s.out.lx : 0.0f;
    *reinterpret_cast<float*>(pad + 0x34) = s.present ? s.out.rx : 0.0f;
    *reinterpret_cast<float*>(pad + 0x38) = s.present ? -s.out.ly : 0.0f;
    *reinterpret_cast<float*>(pad + 0x3C) = s.present ? -s.out.ry : 0.0f;
    if (g_probe && now != s.prev)
        log("[mehrspieler] Takt %u: Port %d Knöpfe %X -> %X\n", input_frame_number(), port, s.prev, now);
    s.prev = now;
    // Rüttelmotoren dieses Spielers (TS2 kopiert sie vor dem Lesen hierher)
    float strength = 1.0f;
    input_pad_vibration(&strength);
    auto scale = [&](int v) {
        const float f = std::clamp(float(v) / 127.0f * strength, 0.0f, 1.0f);
        return WORD(f * 65535.0f + 0.5f);
    };
    s.motor_want[0] = scale(pad[0x45]);
    s.motor_want[1] = scale(pad[0x44]);
}

// ---- Seite "Spieleranzahl"
void choose(int n) { g_choice = n; }
void choose1() { choose(1); }
void choose2() { choose(2); }
void choose3() { choose(3); }
void choose4() { choose(4); }
const ActionFn kChoose[4] = {choose1, choose2, choose3, choose4};

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

void nplayers_build(uint8_t* page) {
    const int pads = pad_count();
    g_menu = g_enabled && pads >= 2;
    g_choice = 0;
    g_applied = false;
    g_desc[1] = g_desc_draw;
    if (!g_menu) {
        g_api.build(page);
        return;
    }
    g_desc[1] = g_img->va<void>(kMenuPageDraw);
    // wie das Hauptmenü (menu.cpp), mit Titel "Select Number of Players"
    *reinterpret_cast<const char**>(page + 0x650) = text(kStrSelectPlayers);
    // Maße und Kopf wie die Seite "Arcade Type" (Werte aus ihrem Aufbau
    // 0x1410C0D8C, mit TS2 Redux dessen Konsolen-Layout)
    *reinterpret_cast<int32_t*>(page + 0x658) = *g_img->va<const int32_t>(kArcadeTypeY);
    *reinterpret_cast<int32_t*>(page + 0x65C) = *g_img->va<const int32_t>(kArcadeTypeH);
    *reinterpret_cast<int32_t*>(page + 0x660) = *g_img->va<const int32_t>(kArcadeTypeHead);
    uint8_t* menu = g_api.menu_create(page, 0x294);
    for (int n = 1; n <= kMaxPads; ++n) {
        uint8_t* e = add_entry(menu, text(kStrNPlayers + 8 * (n - 1)), kChoose[n - 1]);
        if (n > pads) *reinterpret_cast<uint32_t*>(e + 0x44) |= 1;  // ausgegraut: zu wenige Pads
    }
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
    strcpy_s(reinterpret_cast<char*>(menu + 0x21C), 0x80, "");
    *reinterpret_cast<int32_t*>(menu + 0x198) = 1;
    *reinterpret_cast<int32_t*>(menu + 0x194) =
        int(float(*reinterpret_cast<int32_t*>(page + 0x65C)) + *g_api.hint_offset);
    g_api.layout(menu);
    g_api.select(menu, 0);
    g_api.finish(menu);
    *reinterpret_cast<uint32_t*>(page + 0x10) |= 0x10;
    g_api.page_show(page, 0);
    log("[mehrspieler] Takt %u: Spieleranzahl wählen (%d Pads)\n", input_frame_number(), pads);
}

void write_imm(uint64_t va, int32_t v) {
    uint8_t* at = g_img->va(va);
    DWORD old;
    VirtualProtect(at, 4, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(at, &v, 4);
    VirtualProtect(at, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 4);
}

// Der Takt der Homefront-Fassung macht alles Nötige für einen Spieler
// (Profile, Spielerzustände im Menü, weiter ins Hauptmenü); mit der
// gewählten Anzahl an Stelle der festen 1 tut er es für alle.
void run_original_tick(uint8_t* page, int n) {
    write_imm(kTickCountImm, n);
    write_imm(kTickPlayersImm, n);
    g_create_n = 0;
    g_api.tick(page);
    write_imm(kTickCountImm, 1);
    write_imm(kTickPlayersImm, 1);
}

void nplayers_tick(uint8_t* page) {
    // TS2_MP=<n> (Tests): Anzahl ohne Menü
    static const int forced = [] {
        wchar_t v[8] = {};
        return GetEnvironmentVariableW(L"TS2_MP", v, 8) > 0 ? std::clamp(_wtoi(v), 1, kMaxPads) : 0;
    }();
    if (!g_menu && !forced) {
        g_players = 1;
        g_api.tick(page);  // wie bisher
        return;
    }
    const int n = forced ? forced : g_choice;
    if (!n) {
        g_api.page_input(page);
        g_api.page_update(page);
        return;
    }
    // ab der Wahl wie das Original je Takt (die Seite blendet noch aus)
    if (!g_applied) {
        g_applied = true;
        g_players = n;
        freeze_pads();
        for (int p = 1; p < kMaxPads; ++p) input_reset_pad(p), g_slot[p].prev = 0;
        log("[mehrspieler] Takt %u: %d Spieler\n", input_frame_number(), n);
    }
    g_players = n;
    run_original_tick(page, n);
}

void main_menu_tick(uint8_t* page) {
    g_api.main_tick(page);
    if (!g_enabled || (*reinterpret_cast<uint32_t*>(page + 0x10) & 2)) return;  // Seite wechselt gerade
    if (!(g_api.menu_buttons(0) & 0x20)) return;                                // Kreis / B
    if (g_players <= 1 && pad_count() < 2) return;
    log("[mehrspieler] Takt %u: zurück zur Spieleranzahl\n", input_frame_number());
    g_api.page_leave(page);
    g_api.page_goto(g_desc);
}

// Profile im Mehrspieler-Zweig: Spieler 1 bekommt sein gespeichertes Profil
// (profile.cpp, wie mit einem Spieler) und den Windows-Namen, die übrigen
// heißen "Player 2" … (Spieltext). 0x14106A01C legt die Profiltabelle
// vorher jedes Mal neu an (0x14110E3B8), wie mit einem Spieler.
int create_hook(const char*) {
    const int k = g_create_n++;
    static char names[kMaxPads][32];
    char* name = names[std::clamp(k, 0, kMaxPads - 1)];
    if (k == 0) {
        DWORD len = sizeof names[0];
        if (!GetUserNameA(name, &len) || !name[0]) strcpy_s(name, sizeof names[0], text(kStrPlayerN));
        name[15] = 0;
        return (g_api.create_p1 ? g_api.create_p1 : g_api.create)(name);
    }
    strcpy_s(name, sizeof names[0], text(kStrPlayerN + 8 * k));
    return g_api.create(name);
}

// ---- Rennen für Spieler 2–4 (Spieler 1: features.cpp)
uint8_t* player(int i) {
    uint8_t* table = *g_img->va<uint8_t*>(hf2::kPlayerTablePtr);
    if (!table || i >= *g_img->va<int32_t>(hf2::kPlayerCount)) return nullptr;
    return table + size_t(i) * 0x12A0;
}

void walk_hook(void* ctx, float* a, float step1, float* b, float* c, float step2) {
    uint8_t* now = *g_img->va<uint8_t*>(hf2::kPlayerNow);
    float* cap = nullptr;
    float cap_was = 0.0f;
    if (g_players > 1 && now) {
        for (int p = 1; p < g_players && p < kMaxPads; ++p) {
            if (now != player(p) || !g_slot[p].running) continue;
            const int32_t stance = *reinterpret_cast<int32_t*>(now + hf2::kPlayerStance);
            if (stance >= 0 && stance < 4) {
                cap = reinterpret_cast<float*>(now + hf2::kPlayerTopSpeed + 4 * stance);
                cap_was = *cap;
                *cap *= g_run_factor;
            }
            step1 *= g_run_factor;
            step2 *= g_run_factor;
        }
    }
    g_api.walk(ctx, a, step1, b, c, step2);
    if (cap) *cap = cap_was;
}

// Sprung wie features.cpp (Xbox-Fork ts2_jump) für Spieler p
void jump_tick(int p, bool down) {
    Slot& s = g_slot[p];
    uint8_t* pl = player(p);
    const float ticks = *g_img->va<float>(hf2::kFrameTicks);
    float* pos = pl ? reinterpret_cast<float*>(pl + hf2::kPlayerPos) : nullptr;
    const bool pressed = down && !s.jump_was;
    s.jump_was = down;
    if (pressed && pos && s.have_last && ticks > 0.0f) {
        auto* mode = reinterpret_cast<int32_t*>(pl + hf2::kPlayerMoveMode);
        auto* sub = *reinterpret_cast<uint8_t**>(pl + hf2::kPlayerSub);
        uint8_t* chr = sub ? *reinterpret_cast<uint8_t**>(sub + hf2::kCharSub) : nullptr;
        if (*mode == 0 && chr) {
            auto* vx = reinterpret_cast<float*>(pl + hf2::kPlayerFallSpeedX);
            auto* vz = reinterpret_cast<float*>(pl + hf2::kPlayerFallSpeedZ);
            *vx = (pos[0] - s.last[0]) / ticks;
            *vz = (pos[2] - s.last[2]) / ticks;
            if (!(std::fabs(*vx) <= 1.0f && std::fabs(*vz) <= 1.0f)) *vx = *vz = 0.0f;
            *reinterpret_cast<float*>(chr + hf2::kCharVerticalSpeed) = std::sqrt(2.0f * 0.01f * g_jump_height);
            *reinterpret_cast<float*>(chr + hf2::kCharFallStartY) = pos[1];
            *reinterpret_cast<uint32_t*>(pl + hf2::kPlayerFlags10) |= 0x800000u;
            *mode = 2;
            log("[mehrspieler] Spieler %d springt\n", p + 1);
        }
    }
    if (!pos) {
        s.have_last = false;
    } else if (ticks > 0.0f) {
        std::memcpy(s.last, pos, sizeof s.last);
        s.have_last = true;
    }
}

// 2D in Teilansichten. TS2 zeichnet HUD, Texte und Bildschirmeffekte mit
// Bildschirmkoordinaten (Konsole: vortransformierte Ecken, die der
// Bildausschnitt nur beschneidet). Der PC-Port rechnet sie im Zeichnen
// 0x141229434 (spec-Bit 0x20) mit einer festen Matrix für 1920×1080 in den
// Clip-Raum um (Maßstab 0x141220700 mit 2/1920, −2/1080, Verschiebung
// 0x1412206CC mit −1, 1); das gilt nur für eine Ansicht über das ganze Bild.
// In einer Teilansicht landete das HUD von Spieler 1 halb so hoch in der
// oberen Hälfte, und das Nachbild der Sichtart (Kopie des vorigen Bilds,
// über die ganze Ansicht) lag verkleinert als Geisterbild darüber. Mit
// mehreren Spielern bildet die Matrix die Bildschirmkoordinaten deshalb auf
// den gesetzten Bildausschnitt ab (Ziel W×H, Ausschnitt x, y, w, h):
//   sx' = sx·W/w,  tx' = (tx+1)·W/w − 2x/w − 1
//   sy' = sy·H/h,  ty' = ty·H/h + 1 − H/h + 2y/h
// Über das ganze Bild ist das die Original-Matrix.
constexpr uint64_t kOrtho2dTranslateCall = 0x141229681, kMatTranslate = 0x1412206CC;
using MatTranslateFn = void (*)(float* m, float tx, float ty, float tz);
MatTranslateFn g_mat_translate = nullptr;

void ortho2d_translate_hook(float* m, float tx, float ty, float tz) {
    g_mat_translate(m, tx, ty, tz);
    static const bool off = GetEnvironmentVariableW(L"TS2_MP2D_ALT", nullptr, 0) > 0;  // Vergleich: wie vorher
    if (g_players <= 1 || off) return;
    void** holder = *g_img->va<void**>(0x143020AD0);
    auto* ctx = holder ? static_cast<ID3D11DeviceContext*>(holder[0]) : nullptr;
    auto* rtv = *g_img->va<ID3D11RenderTargetView*>(kRtv);
    if (!ctx || !rtv) return;
    D3D11_VIEWPORT vp{};
    UINT n = 1;
    ctx->RSGetViewports(&n, &vp);
    if (n < 1 || vp.Width <= 0.0f || vp.Height <= 0.0f) return;
    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    D3D11_TEXTURE2D_DESC d{};
    if (ID3D11Texture2D* tex = nullptr; res && SUCCEEDED(res->QueryInterface(&tex))) tex->GetDesc(&d), tex->Release();
    if (res) res->Release();
    const float W = float(d.Width), H = float(d.Height);
    if (W <= 0.0f || H <= 0.0f) return;
    if (vp.TopLeftX == 0.0f && vp.TopLeftY == 0.0f && vp.Width == W && vp.Height == H) return;
    const float kx = W / vp.Width, ky = H / vp.Height;
    m[0] *= kx;                                                            // +0x00 sx
    m[5] *= ky;                                                            // +0x14 sy
    m[12] = (m[12] + 1.0f) * kx - 2.0f * vp.TopLeftX / vp.Width - 1.0f;   // +0x30 tx
    m[13] = m[13] * ky + 1.0f - ky + 2.0f * vp.TopLeftY / vp.Height;     // +0x34 ty
}

// Teilansicht löschen: Farbe nur im Rechteck (ClearView, D3D 11.1), Tiefe
// und Schablone ganz (die Ansichten werden nacheinander gezeichnet; was eine
// fertige Ansicht an Tiefe hatte, braucht danach niemand)
ID3D11DeviceContext1* g_ctx1 = nullptr;
bool g_ctx1_tried = false;

void clear_hook(int x0, int y0, int x1, int y1, uint32_t rgba) {
    auto* rtv = *g_img->va<ID3D11RenderTargetView*>(kRtv);
    if (g_players <= 1 || !rtv) return g_api.clear(x0, y0, x1, y1, rgba);
    if (!g_ctx1_tried) {
        g_ctx1_tried = true;
        void** holder = *g_img->va<void**>(kDeviceHolder);
        auto* dev = holder ? static_cast<ID3D11Device*>(holder[0]) : nullptr;
        ID3D11DeviceContext* ctx = nullptr;
        if (dev) dev->GetImmediateContext(&ctx);
        if (ctx) {
            ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&g_ctx1));
            ctx->Release();
        }
        log("[mehrspieler] Teilansichten löschen: %s\n", g_ctx1 ? "ClearView" : "nicht möglich (ganz)");
    }
    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    D3D11_TEXTURE2D_DESC d{};
    if (ID3D11Texture2D* tex = nullptr; res && SUCCEEDED(res->QueryInterface(&tex))) tex->GetDesc(&d), tex->Release();
    if (res) res->Release();
    const bool full = x0 <= 0 && y0 <= 0 && x1 >= int(d.Width) && y1 >= int(d.Height);
    if (full || !g_ctx1) return g_api.clear(x0, y0, x1, y1, rgba);
    const float c[4] = {float((rgba >> 24) & 0xFF) / 255.0f, float((rgba >> 16) & 0xFF) / 255.0f,
                        float((rgba >> 8) & 0xFF) / 255.0f, float(rgba & 0xFF) / 255.0f};
    const D3D11_RECT r{std::max(x0, 0), std::max(y0, 0), std::min(x1, int(d.Width)), std::min(y1, int(d.Height))};
    if (r.right > r.left && r.bottom > r.top) g_ctx1->ClearView(rtv, c, &r, 1);
    if (auto* dsv = *g_img->va<ID3D11DepthStencilView*>(kDsv)) {
        void** holder = *g_img->va<void**>(0x143020AD0);
        auto* ctx = holder ? static_cast<ID3D11DeviceContext*>(holder[0]) : nullptr;
        if (ctx) ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }
}

}  // namespace

bool mp_active() { return g_players > 1; }
int mp_players() { return g_players; }

int mp_pad_user(int player) { return player >= 0 && player < kMaxPads && g_players > 1 ? g_user[player] : -1; }

void mp_frame() {
    if (!g_img || g_players <= 1) return;  // Einzelspieler: nichts zu tun
    const uint32_t frame = input_frame_number();
    assign_pads();
    for (int p = 1; p < kMaxPads; ++p) {
        Slot& s = g_slot[p];
        XINPUT_GAMEPAD g;
        uint32_t raw = 0;
        const bool on = p < g_players && pad_state(p, frame, &g, &raw);
        s.out = on ? input_translate_pad(g, p) : PadOutput{};
        s.out.bits |= on ? raw : 0;
        s.present = s.present && on;
        motors_apply(s, on && p < g_players);
        if (p >= g_players) continue;
        // Rennen und Springen ([bewegung], wie Spieler 1)
        const bool run_down = g_run && (s.out.host & kHostRun);
        if (g_run_toggle) {
            if (run_down && !s.run_was) s.run_latched = !s.run_latched;
            s.running = s.run_latched;
        } else {
            s.running = run_down;
        }
        s.run_was = run_down;
        jump_tick(p, g_jump && (s.out.host & kHostJump));
    }
    // TS2_MPPROBE=1: alle 60 Takte Lage und Blick der Spieler
    if (g_probe && frame % 60 == 0) {
        const int n = *g_img->va<int32_t>(hf2::kPlayerCount);
        for (int i = 0; i < n && i < kMaxPads; ++i)
            if (uint8_t* pl = player(i)) {
                const float* q = reinterpret_cast<const float*>(pl + hf2::kPlayerPos);
                log("[mehrspieler] Takt %u: Spieler %d bei %.2f %.2f %.2f, Gier %.1f\n", frame, i + 1, q[0], q[1], q[2],
                    *reinterpret_cast<const float*>(pl + 0x154));
            }
    }
}

void mp_install(const Ts2Image& img, const std::filesystem::path& ini) {
    g_img = &img;
    const std::wstring p = ini.wstring();
    g_enabled = GetPrivateProfileIntW(L"mehrspieler", L"an", 1, p.c_str()) != 0;
    g_jump = GetPrivateProfileIntW(L"bewegung", L"springen", 1, p.c_str()) != 0;
    wchar_t buf[64];
    GetPrivateProfileStringW(L"bewegung", L"sprunghoehe", L"1.2", buf, 64, p.c_str());
    g_jump_height = float(_wtof(buf));
    g_run = GetPrivateProfileIntW(L"bewegung", L"rennen", 1, p.c_str()) != 0;
    g_run_factor =
        float(std::clamp(int(GetPrivateProfileIntW(L"bewegung", L"renntempo", 160, p.c_str())), 100, 300)) / 100.0f;
    g_run_toggle = GetPrivateProfileIntW(L"bewegung", L"rennen_umschalten", 0, p.c_str()) != 0;
    script_load();
    g_probe = GetEnvironmentVariableW(L"TS2_MPPROBE", nullptr, 0) > 0;

    auto fn = [&](uint64_t va) { return img.va<void>(va); };
    g_api.menu_create = reinterpret_cast<MenuCreateFn>(fn(hf2::kMenuCreate));
    g_api.add_entry = reinterpret_cast<MenuAddEntryFn>(fn(hf2::kMenuAddEntry));
    g_api.entry_width = reinterpret_cast<MenuIntFn>(fn(hf2::kEntryWidth));
    g_api.menu_x = reinterpret_cast<MenuIntFn>(fn(hf2::kMenuX));
    g_api.menu_y = reinterpret_cast<MenuIntFn>(fn(hf2::kMenuY));
    g_api.layout = reinterpret_cast<MenuVoidFn>(fn(hf2::kMenuLayout));
    g_api.finish = reinterpret_cast<MenuVoidFn>(fn(hf2::kMenuFinish));
    g_api.select = reinterpret_cast<MenuVoidIntFn>(fn(hf2::kMenuSelect));
    g_api.page_show = reinterpret_cast<MenuVoidIntFn>(fn(hf2::kPageShow));
    g_api.page_input = reinterpret_cast<PageFn>(fn(kPageInput));
    g_api.page_update = reinterpret_cast<PageFn>(fn(kPageUpdate));
    g_api.y_offset = img.va<float>(hf2::kMenuYOffset);
    g_api.hint_offset = img.va<float>(hf2::kMenuHintOffset);
    g_api.string_tables = img.va<uint8_t* const>(hf2::kStringTables);
    g_api.language = img.va<int32_t>(hf2::kLanguage);
    g_api.fe_players = img.va<int32_t>(hf2::kPlayersInGame);

    // Prüfen, dass der Code ist wie erwartet
    const uint8_t* cnt = img.va<const uint8_t>(kTickCountImm - 1);
    const uint8_t* ply = img.va<const uint8_t>(kTickPlayersImm - 6);
    const bool code_ok = cnt[0] == 0xB9 && *reinterpret_cast<const int32_t*>(cnt + 1) == 1 && ply[0] == 0xC7 &&
                         ply[1] == 0x05 && *reinterpret_cast<const int32_t*>(ply + 6) == 1 &&
                         int64_t(ply + 10 + *reinterpret_cast<const int32_t*>(ply + 2)) ==
                             int64_t(img.va<uint8_t>(hf2::kPlayersInGame));
    void** page = img.va<void*>(kNPlayersPage);
    if (!code_ok || page[0] != img.va<void>(kNPlayersTick) || page[4] != img.va<void>(kNPlayersBuild)) {
        log("[mehrspieler] Seite der Spieleranzahl nicht wie erwartet, kein Mehrspieler\n");
        return;
    }
    g_api.pad_read = reinterpret_cast<PadReadFn>(
        hook_call(img, kPadReadCall, kPadRead, reinterpret_cast<const void*>(&pad_read_hook)));
    g_api.pad_scan = reinterpret_cast<PadScanFn>(
        hook_call(img, kPadScanCall, kPadScan, reinterpret_cast<const void*>(&pad_scan_hook)));
    if (!g_api.pad_read || !g_api.pad_scan) {
        log("[mehrspieler] Pad-Lesen nicht gefunden, kein Mehrspieler\n");
        return;
    }
    // Profil von Spieler 1: dieselbe Funktion wie im Einspieler-Zweig
    // (profile.cpp hat den Aufruf dort auf sich gelenkt)
    {
        const uint8_t* site = img.va<const uint8_t>(kProfileCreateCall1);
        if (site[0] == 0xE8) {
            const int32_t rel = *reinterpret_cast<const int32_t*>(site + 1);
            g_api.create_p1 = reinterpret_cast<CreateFn>(const_cast<uint8_t*>(site + 5 + rel));
        }
    }
    g_api.create = reinterpret_cast<CreateFn>(
        hook_call(img, kProfileCreateCallN, kProfileCreate, reinterpret_cast<const void*>(&create_hook)));
    // Rennen der Spieler 2–4: vor den Umweg von features.cpp (Spieler 1)
    {
        const uint8_t* site = img.va<const uint8_t>(hf2::kWalkCall);
        if (site[0] == 0xE8) {
            const int32_t rel = *reinterpret_cast<const int32_t*>(site + 1);
            const uint64_t target = img.preferred_base + uint64_t(site + 5 + rel - img.base);
            g_api.walk = reinterpret_cast<WalkFn>(
                hook_call(img, hf2::kWalkCall, target, reinterpret_cast<const void*>(&walk_hook)));
        }
    }
    int clears = 0;
    for (uint64_t va : kClearCalls)
        if (void* orig = hook_call(img, va, kClear, reinterpret_cast<const void*>(&clear_hook)))
            g_api.clear = reinterpret_cast<ClearFn>(orig), ++clears;
    g_mat_translate = reinterpret_cast<MatTranslateFn>(hook_call(img, kOrtho2dTranslateCall, kMatTranslate,
                                                                 reinterpret_cast<const void*>(&ortho2d_translate_hook)));
    if (!g_mat_translate) log("[mehrspieler] 2D-Matrix (0x141229681) nicht gefunden, HUD in Teilansichten verschoben\n");
    g_desc = page;
    g_desc_draw = page[1];
    g_api.tick = reinterpret_cast<PageFn>(page[0]);
    g_api.build = reinterpret_cast<PageFn>(page[4]);
    void** main_page = img.va<void*>(hf2::kMainMenuPage);
    if (main_page[0] == img.va<void>(kMainMenuTick)) {
        g_api.main_tick = reinterpret_cast<PageFn>(main_page[0]);
        g_api.page_leave = reinterpret_cast<PageFn>(fn(kPageLeave));
        g_api.menu_buttons = reinterpret_cast<int (*)(int)>(fn(kMenuButtons));
        g_api.page_goto = reinterpret_cast<void* (*)(void*)>(fn(kPageGoto));
        main_page[0] = reinterpret_cast<void*>(&main_menu_tick);
    }
    page[0] = reinterpret_cast<void*>(&nplayers_tick);
    page[4] = reinterpret_cast<void*>(&nplayers_build);
    std::atexit(motors_off_at_exit);
    log("[mehrspieler] bereit (%s), bis %d Pads; Profil Spieler 1 %s, Rennen 2-4 %s, Löschen %d/7\n",
        g_enabled ? "an" : "aus", kMaxPads, g_api.create_p1 ? "aus profil.bin" : "neu", g_api.walk ? "ja" : "nein",
        clears);
}

}  // namespace host
