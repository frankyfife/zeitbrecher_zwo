#include "input.h"

#include <xinput.h>

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "log.h"
#include "ts2options.h"
#include "menumouse.h"
#include "multiplayer.h"

#pragma comment(lib, "xinput.lib")

namespace host {
namespace {

// ---- Methoden des TS2-Laufzeitobjekts (vtable 0x1421EAE20)
enum : uint32_t {
    kSetMode = 0x50,      // int: 1 = Tastatur+Maus (Port 6), 0 = Pad (Port 0)
    kConnect = 0x58,      // Pad verbunden
    kSetLeftX = 0x68,     // float -1..1, rechts positiv
    kSetLeftY = 0x70,     // float -1..1, vorwärts positiv
    kLookX = 0x78,        // float: Pad: rechter Stick X; Mausmodus: Mausbewegung X (addiert)
    kLookY = 0x80,        // float: Pad: rechter Stick Y; Mausmodus: Mausbewegung Y, negiert
    kSetButtons = 0x88,   // int: Bitmaske unten
};

// ---- Tastenbits (aus Homefronts Übersetzer 0x14015F880)
enum : uint32_t {
    kL2 = 0x1, kR2 = 0x2, kL1 = 0x4, kR1 = 0x8,
    kTriangle = 0x10, kCircle = 0x20 /* auch "Zurück" */, kCross = 0x40 /* auch "OK" */, kSquare = 0x80,
    kStart = 0x100,  // Pause / Select
    kUp = 0x1000, kRight = 0x2000, kDown = 0x4000, kLeft = 0x8000,
    // nur Tastatur+Maus (Port 6)
    kFire = 0x10000, kFireAlt = 0x20000, kActivate = 0x40000, kReload = 0x80000, kCrouch = 0x100000,
    kZoom = 0x200000, kNextWeapon = 0x400000, kPrevWeapon = 0x800000, kZoomIn = 0x1000000, kZoomOut = 0x2000000,
};

struct KeyBind {
    const wchar_t* name;  // Schlüssel in ts2.ini [tasten]
    uint32_t bit;
    int vk[3];            // bis zu drei Tasten; Vorgabe aus ts2opt::key_defaults()
};
// Aktion -> TS2-Bit. Die Vorgaben (wie die PC-Belegung des Xbox-Forks)
// stehen in src/common/ts2options.cpp, gemeinsam mit dem Launcher.
KeyBind g_keys[] = {
    {L"feuer", kFire, {}},          {L"alt_feuer", kFireAlt, {}},     {L"zoom", kZoom, {}},
    {L"benutzen", kActivate, {}},   {L"nachladen", kReload, {}},     {L"ducken", kCrouch, {}},
    {L"naechste_waffe", kNextWeapon, {}},  // dazu Mausrad nach unten
    {L"vorige_waffe", kPrevWeapon, {}},    // dazu Mausrad nach oben
    {L"zoom_rein", kZoomIn, {}},    {L"zoom_raus", kZoomOut, {}},    {L"pause", kStart, {}},
    {L"ok", kCross, {}},            {L"zurueck", kCircle, {}},       {L"dreieck", kTriangle, {}},
    {L"quadrat", kSquare, {}},      {L"menue_hoch", kUp, {}},        {L"menue_runter", kDown, {}},
    {L"menue_links", kLeft, {}},    {L"menue_rechts", kRight, {}},
};
constexpr int kBindingVersion = ts2opt::kBindingVersion;

// Vorgabe einer Taste aus der gemeinsamen Tabelle
const int* default_vk(const wchar_t* name) {
    for (const auto& d : ts2opt::key_defaults()) {
        const char* a = d.key;
        const wchar_t* b = name;
        while (*a && wchar_t(*a) == *b) ++a, ++b;
        if (!*a && !*b) return d.vk;
    }
    static const int none[3] = {};
    return none;
}

std::wstring vk_list(const int* vk) {
    std::wstring s;
    for (int i = 0; i < 3 && vk[i]; ++i) s += (s.empty() ? L"" : L",") + std::to_wstring(vk[i]);
    return s.empty() ? L"0" : s;
}
int g_fwd = 0, g_back = 0, g_left = 0, g_right = 0;  // Vorgaben aus ts2opt (input_init)
float g_mouse_sens = 1.0f;
bool g_mouse_invert = false;

HWND g_wnd = nullptr;
void* g_obj = nullptr;
int g_mode = -1;          // zuletzt an TS2 gemeldeter Modus
bool g_use_pad = false;   // zuletzt benutztes Gerät
float g_mdx = 0, g_mdy = 0;
int g_wheel = 0;          // +1/-1 je Raste, als Tastendruck für einen Takt
bool g_captured = false;
uint32_t g_frame = 0;

// Bits nur für Testskripte (nicht an TS2): Sprung, vorwärts gehen
constexpr uint32_t kScriptForward = kHostForward,
                   kHostBits = kHostJump | kHostForward | kHostRun | kHostMouseRight | kHostFreecam | kHostWheelUp | kHostWheelDown;
uint32_t g_host_bits = 0;
bool g_freecam = false;
FreecamInput g_fc;

struct Scripted {
    uint32_t from, count, bits;
};
std::vector<Scripted> g_script;

template <class... A>
void call(uint32_t off, A... a) {
    using Fn = void(__fastcall*)(void*, A...);
    reinterpret_cast<Fn>((*reinterpret_cast<void***>(g_obj))[off / 8])(g_obj, a...);
}

bool down(int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000) != 0; }
bool focused() { return GetForegroundWindow() == g_wnd; }

void capture(bool on) {
    if (on == g_captured) return;
    g_captured = on;
    if (on) {
        RECT r;
        GetClientRect(g_wnd, &r);
        MapWindowPoints(g_wnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
        ClipCursor(&r);
        while (ShowCursor(FALSE) >= 0) {}
    } else {
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) < 0) {}
    }
}

// ---- Testskript fürs Pad (Umgebung TS2_PADSKRIPT): ersetzt XInput.
// "takt:knoepfe,lx,ly,rx,ry,lt,rt[,ts2bits];…": ab diesem Takt gilt der
// Zustand (XINPUT_GAMEPAD, Knöpfe und ts2bits hexadezimal, Sticks
// -32768..32767, Trigger 0..255) bis zum nächsten Eintrag; ts2bits gehen
// zusätzlich unverändert an TS2 (Proben).
struct PadStep {
    uint32_t tick;
    XINPUT_GAMEPAD g;
    uint32_t raw;
};
std::vector<PadStep> g_pad_script;
bool g_pad_scripted = false;
uint32_t g_pad_raw = 0;

// ---- Testskript für die Maus (Umgebung TS2_MAUSSKRIPT, nur für Messungen):
// "bild:dx,dy;…": ab diesem Bild kommen je Bild dx/dy Zählschritte dazu
// (wie Raw Input, auch Bruchteile) bis zum nächsten Eintrag.
struct MouseStep {
    uint32_t frame;
    float dx, dy;
};
std::vector<MouseStep> g_mouse_script;
float g_last_look[2] = {};  // zuletzt an TS2 gegebene Mausbewegung (Proben)

// ---- Maus 1:1 (Mausmodus, Port 6)
// TS2 summiert die Mausbewegung (vtable +0x78/+0x80) in 0x143F0F4A8 (x) und
// 0x143F0F4A4 (y); scePadRead (0x141225898) gibt je Lesung die Differenz zur
// vorigen Lesung (0x143F0F4AC/0x143F0F4B4) × 0,2 (0x14259CF44) als Pad
// +0x34/+0x3C. Gemessen (TS2_MAUSSKRIPT/TS2_MAUSPROBE, Siberia):
// - linear, ohne Glättung, Kappung oder Beschleunigung, wirkt im selben Bild;
// - Gier 0,5333° je Zählschritt, Nick nur 0,2667° (halb so empfindlich);
// - beides mal Bildzeit (0x14259CBE4): bei takt=120 (0,5) halb so viel
//   Drehung je Zählschritt wie bei 60, in einem Bild mit zwei Takten doppelt.
// ts2.exe teilt deshalb durch die Bildzeit und gibt Nick doppelt: dann dreht
// ein Zählschritt immer 0,5333° × empfindlichkeit, waagerecht wie senkrecht
// und bei jedem Takt (wie Gier bei 60 Hz bisher). Die Summen setzt ts2.exe
// je Bild auf den noch nicht gelesenen Rest zurück; sonst wüchsen sie
// unbegrenzt, und float verlöre Nachkommastellen.
constexpr uint64_t kMouseAccX = 0x143F0F4A8, kMouseLastX = 0x143F0F4AC;
constexpr uint64_t kMouseAccY = 0x143F0F4A4, kMouseLastY = 0x143F0F4B4;
constexpr uint64_t kFrameTicksRun = 0x14259CBE4;  // float, Bildzeit in Takten (Pause 0)
constexpr float kPitchMatch = 2.0f;
const Ts2Image* g_img = nullptr;

// Faktor für die Mausbewegung dieses Bildes: 1 / Bildzeit
float mouse_time_scale() {
    if (!g_img) return 1.0f;
    const float dt = *g_img->va<float>(kFrameTicksRun);
    return dt > 0.01f ? 1.0f / dt : 1.0f;  // Pause: Bildzeit 0, Blick steht ohnehin
}

void mouse_rebase() {
    if (!g_img) return;
    for (auto [acc, last] : {std::pair{kMouseAccX, kMouseLastX}, std::pair{kMouseAccY, kMouseLastY}}) {
        float* a = g_img->va<float>(acc);
        float* l = g_img->va<float>(last);
        *a -= *l;
        *l = 0.0f;
    }
}

void mouse_script_load() {
    wchar_t buf[8192] = {};
    if (!GetEnvironmentVariableW(L"TS2_MAUSSKRIPT", buf, 8192)) return;
    for (const wchar_t* p = buf; *p;) {
        MouseStep st{};
        if (swscanf(p, L"%u:%f,%f", &st.frame, &st.dx, &st.dy) == 3) g_mouse_script.push_back(st);
        p = wcschr(p, L';');
        if (!p) break;
        ++p;
    }
    log("Maus-Skript: %zu Einträge\n", g_mouse_script.size());
}

void pad_script_load() {
    wchar_t buf[4096] = {};
    if (!GetEnvironmentVariableW(L"TS2_PADSKRIPT", buf, 4096)) return;
    g_pad_scripted = true;
    std::wstring s = buf;
    size_t i = 0;
    while (i < s.size()) {
        size_t e = s.find(L';', i);
        std::wstring part = s.substr(i, e == std::wstring::npos ? std::wstring::npos : e - i);
        PadStep st{};
        unsigned b = 0, raw = 0;
        int lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
        if (swscanf(part.c_str(), L"%u:%x,%d,%d,%d,%d,%d,%d,%x", &st.tick, &b, &lx, &ly, &rx, &ry, &lt, &rt, &raw) >= 2) {
            st.g.wButtons = WORD(b);
            st.g.sThumbLX = SHORT(std::clamp(lx, -32768, 32767));
            st.g.sThumbLY = SHORT(std::clamp(ly, -32768, 32767));
            st.g.sThumbRX = SHORT(std::clamp(rx, -32768, 32767));
            st.g.sThumbRY = SHORT(std::clamp(ry, -32768, 32767));
            st.g.bLeftTrigger = BYTE(std::clamp(lt, 0, 255));
            st.g.bRightTrigger = BYTE(std::clamp(rt, 0, 255));
            st.raw = raw;
            g_pad_script.push_back(st);
        }
        if (e == std::wstring::npos) break;
        i = e + 1;
    }
    log("Pad-Skript: %zu Einträge\n", g_pad_script.size());
}

// wie XInputGetState für das erste verbundene Pad; mit Skript dessen Zustand
bool pad_state(XINPUT_STATE* xs, DWORD* port) {
    if (g_pad_scripted) {
        const PadStep* cur = nullptr;
        for (auto& st : g_pad_script)
            if (st.tick <= g_frame) cur = &st;
        static const PadStep* last = nullptr;
        static DWORD packet = 0;
        if (cur != last) ++packet, last = cur;
        *xs = {};
        xs->dwPacketNumber = packet;
        if (cur) xs->Gamepad = cur->g;
        g_pad_raw = cur ? cur->raw : 0;
        *port = 0;
        return true;
    }
    // Mehrspieler: Spieler 1 behält das Pad, das er bei der Wahl hatte
    if (const int u = mp_pad_user(0); u >= 0) {
        *port = DWORD(u);
        return XInputGetState(DWORD(u), xs) == ERROR_SUCCESS;
    }
    for (DWORD i = 0; i < 4; ++i)
        if (XInputGetState(i, xs) == ERROR_SUCCESS) {
            *port = i;
            return true;
        }
    return false;
}

// ---- Pad-Belegung ([pad] in ts2.ini, Vorgaben in src/common/ts2options.cpp)
// TS2s Steuerung im Spiel liest feste Masken im Spieler (+0x20, gefüllt von
// 0x1411DCBAC): Feuer 0x10000, Alt-Feuer 0x20000 … – auch im Pad-Modus. Die
// Konsolen-Bits (L1, R1, Dreieck …) bewegen im Spiel nichts; sie dienen nur
// den Menüs. Das Pad gibt deshalb dieselben Bits wie Tastatur und Maus.
// Quellen: Bits 0..15 = XINPUT_GAMEPAD-Knöpfe, dazu die Trigger.
constexpr uint32_t kSrcLT = 1u << 16, kSrcRT = 1u << 17;
struct PadName {
    const wchar_t* name;
    uint32_t mask;
};
const PadName kPadNames[] = {
    {L"A", XINPUT_GAMEPAD_A},         {L"B", XINPUT_GAMEPAD_B},
    {L"X", XINPUT_GAMEPAD_X},         {L"Y", XINPUT_GAMEPAD_Y},
    {L"LB", XINPUT_GAMEPAD_LEFT_SHOULDER}, {L"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER},
    {L"LT", kSrcLT},                  {L"RT", kSrcRT},
    {L"L3", XINPUT_GAMEPAD_LEFT_THUMB}, {L"R3", XINPUT_GAMEPAD_RIGHT_THUMB},
    {L"Start", XINPUT_GAMEPAD_START}, {L"Back", XINPUT_GAMEPAD_BACK},
    {L"Hoch", XINPUT_GAMEPAD_DPAD_UP}, {L"Runter", XINPUT_GAMEPAD_DPAD_DOWN},
    {L"Links", XINPUT_GAMEPAD_DPAD_LEFT}, {L"Rechts", XINPUT_GAMEPAD_DPAD_RIGHT},
};
struct PadBind {
    const wchar_t* key;    // Schlüssel in [pad]
    uint32_t bits;         // an TS2
    uint32_t host;         // an ts2.exe (Rennen, Sprung, freie Kamera)
    uint32_t combo[2];     // bis zu zwei Belegungen ("RT" oder "L3+R3", mit Komma)
};
PadBind g_pad_binds[] = {
    {L"feuer", kFire, 0, {}},          {L"alt_feuer", kFireAlt, 0, {}},   {L"zielen", kZoom, 0, {}},
    {L"ducken", kCrouch, 0, {}},       {L"benutzen", kActivate, 0, {}},   {L"nachladen", kReload, 0, {}},
    {L"naechste_waffe", kNextWeapon, 0, {}}, {L"vorige_waffe", kPrevWeapon, 0, {}},
    {L"zoom_rein", kZoomIn, 0, {}},    {L"zoom_raus", kZoomOut, 0, {}},   {L"pause", kStart, 0, {}},
    {L"dreieck", kTriangle, 0, {}},    {L"quadrat", kSquare, 0, {}},
    {L"rennen", 0, kHostRun, {}},      {L"springen", 0, kHostJump, {}},   {L"freecam", 0, kHostFreecam, {}},
};

uint32_t parse_combo(const std::wstring& s) {
    uint32_t m = 0;
    size_t i = 0;
    while (i <= s.size()) {
        size_t e = s.find(L'+', i);
        if (e == std::wstring::npos) e = s.size();
        std::wstring n = s.substr(i, e - i);
        while (!n.empty() && n.front() == L' ') n.erase(n.begin());
        while (!n.empty() && n.back() == L' ') n.pop_back();
        for (const auto& p : kPadNames)
            if (!n.empty() && _wcsicmp(n.c_str(), p.name) == 0) m |= p.mask;
        i = e + 1;
    }
    return m;
}

struct PadConfig {
    bool on = true;
    bool vibration = true;
    float vib_strength = 1.0f;
    float deadzone = 0.24f;  // Anteil des Stickwegs
    float look_sens = 1.0f;
    bool invert = false;
    bool aim_toggle = true;  // Zielen schaltet um (Xbox: R3-Klick) statt gehalten
} g_pad;

DWORD g_pad_port = 0;
bool g_pad_present = false;

// Zustand der Übersetzung je Pad (Spieler 1 und, im Mehrspieler, 2–4)
struct PadTrack {
    uint32_t chord_lock = 0;   // Knöpfe eines gehaltenen Akkords (gehen nicht einzeln an TS2)
    bool aim_latched = false;  // Zielen umgeschaltet an
    bool aim_was = false;
    int held[18] = {}, pulse[18] = {};
    bool chorded[18] = {};
};
PadTrack g_tracks[kMaxPads];
PadTrack& g_p1 = g_tracks[0];

void pad_load(const std::wstring& ini) {
    const wchar_t* p = ini.c_str();
    auto def = [](const char* key) {
        const std::string v = ts2opt::default_value("pad", key);
        return std::wstring(v.begin(), v.end());
    };
    auto getf = [&](const wchar_t* key, const char* dkey, float lo, float hi) {
        wchar_t buf[32];
        GetPrivateProfileStringW(L"pad", key, def(dkey).c_str(), buf, 32, p);
        return std::clamp(float(_wtof(buf)), lo, hi);
    };
    g_pad.on = GetPrivateProfileIntW(L"pad", L"an", 1, p) != 0;
    g_pad.vibration = GetPrivateProfileIntW(L"pad", L"vibration", 1, p) != 0;
    g_pad.vib_strength = getf(L"vibration_staerke", "vibration_staerke", 0.0f, 200.0f) / 100.0f;
    g_pad.deadzone = getf(L"totzone", "totzone", 0.0f, 90.0f) / 100.0f;
    g_pad.look_sens = getf(L"empfindlichkeit", "empfindlichkeit", 0.1f, 3.0f);
    g_pad.invert = GetPrivateProfileIntW(L"pad", L"umkehren", 0, p) != 0;
    g_pad.aim_toggle = GetPrivateProfileIntW(L"pad", L"zielen_umschalten", 1, p) != 0;
    for (auto& b : g_pad_binds) {
        char key[32] = {};
        for (int i = 0; b.key[i] && i < 31; ++i) key[i] = char(b.key[i]);
        wchar_t buf[64];
        GetPrivateProfileStringW(L"pad", b.key, def(key).c_str(), buf, 64, p);
        std::wstring s = buf;
        const size_t c = s.find(L',');
        b.combo[0] = parse_combo(s.substr(0, c));
        b.combo[1] = c == std::wstring::npos ? 0 : parse_combo(s.substr(c + 1));
    }
}

// Knöpfe, die zu einem Akkord gehören (L3 und R3 bei L3+R3), gehen erst
// nach kChordWait Takten einzeln an TS2, damit der Akkord nicht zuerst
// ducken oder zielen auslöst (beides schaltet um); ein kürzerer Klick geht
// beim Loslassen als kurzer Druck hinaus.
constexpr int kChordWait = 4, kChordPulse = 2;
uint32_t chord_members_delay(uint32_t src, PadTrack& t) {
    int* held = t.held;
    int* pulse = t.pulse;
    bool* chorded = t.chorded;
    uint32_t members = 0;
    for (const auto& pb : g_pad_binds)
        for (uint32_t c : pb.combo)
            if (c & (c - 1)) members |= c;
    uint32_t out = src & ~members & ~t.chord_lock;
    for (int i = 0; i < 18; ++i) {
        const uint32_t m = 1u << i;
        if (!(members & m)) continue;
        if (src & m) {
            if (t.chord_lock & m) chorded[i] = true;
            ++held[i];
            if (!chorded[i] && held[i] >= kChordWait) out |= m;
        } else {
            if (held[i] > 0 && held[i] < kChordWait && !chorded[i]) pulse[i] = kChordPulse;
            held[i] = 0;
            chorded[i] = false;
        }
        if (pulse[i] > 0) --pulse[i], out |= m;
    }
    return out;
}

// Sticks mit eigener Totzone (rund, je Stick), Ausgabe -1..1
void stick2(SHORT x, SHORT y, float dead, float* ox, float* oy) {
    const float fx = std::fmax(float(x), -32767.0f) / 32767.0f, fy = std::fmax(float(y), -32767.0f) / 32767.0f;
    const float len = std::sqrt(fx * fx + fy * fy);
    if (len <= dead || len <= 0.0f) {
        *ox = *oy = 0.0f;
        return;
    }
    const float scale = std::fmin((len - dead) / (1.0f - dead), 1.0f) / len;
    *ox = fx * scale;
    *oy = fy * scale;
}

// Ein Pad nach [pad] in TS2-Bits, Wirt-Bits und Sticks (−1..1, Blick noch
// unskaliert) übersetzen, mit eigenem Zustand je Pad (Akkorde, Zielen)
PadOutput translate(const XINPUT_GAMEPAD& g, PadTrack& t) {
    PadOutput o;
    stick2(g.sThumbLX, g.sThumbLY, g_pad.deadzone, &o.lx, &o.ly);
    stick2(g.sThumbRX, g.sThumbRY, g_pad.deadzone, &o.rx, &o.ry);
    // Quellen (Knöpfe und Trigger); ein gehaltener Akkord (z. B. L3+R3)
    // nimmt seine Knöpfe, bis alle losgelassen sind
    uint32_t src = g.wButtons;
    if (g.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) src |= kSrcLT;
    if (g.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) src |= kSrcRT;
    for (const auto& pb : g_pad_binds)
        for (uint32_t c : pb.combo)
            if ((c & (c - 1)) && (src & c) == c) t.chord_lock |= c;
    for (const auto& pb : g_pad_binds)
        for (uint32_t c : pb.combo)
            if ((c & (c - 1)) && !(src & c)) t.chord_lock &= ~c;
    const uint32_t single = chord_members_delay(src, t);
    uint32_t bits = 0;
    for (const auto& pb : g_pad_binds)
        for (uint32_t c : pb.combo) {
            if (!c) continue;
            const bool on = (c & (c - 1)) ? (src & c) == c : (single & c) != 0;
            if (on) bits |= pb.bits, o.host |= pb.host;
        }
    // Menüs: A OK, B zurück, Steuerkreuz (fest, wie die Konsole)
    if (single & XINPUT_GAMEPAD_A) bits |= kCross;
    if (single & XINPUT_GAMEPAD_B) bits |= kCircle;
    if (single & XINPUT_GAMEPAD_START) bits |= kStart;
    if (single & XINPUT_GAMEPAD_DPAD_UP) bits |= kUp;
    if (single & XINPUT_GAMEPAD_DPAD_DOWN) bits |= kDown;
    if (single & XINPUT_GAMEPAD_DPAD_LEFT) bits |= kLeft;
    if (single & XINPUT_GAMEPAD_DPAD_RIGHT) bits |= kRight;
    // Zielen: TS2 zielt, solange das Bit gehalten wird (rechte Maustaste).
    // Am Pad schaltet ein Druck um wie auf der Xbox (R3); ein
    // Waffenwechsel oder die freie Kamera beendet es.
    if (g_pad.aim_toggle) {
        const bool now = (bits & kZoom) != 0;
        if (now && !t.aim_was) t.aim_latched = !t.aim_latched;
        t.aim_was = now;
        if (bits & (kNextWeapon | kPrevWeapon)) t.aim_latched = false;
        bits = (bits & ~kZoom) | (t.aim_latched ? kZoom : 0);
    }
    o.bits = bits;
    return o;
}

// Rüttelmotoren: zuletzt gesetzt und gewünscht (0..65535)
WORD g_motor_set[2] = {}, g_motor_want[2] = {};
bool g_motor_valid = false;

void motors_apply(bool active) {
    WORD l = 0, r = 0;
    if (active && g_pad.vibration) l = g_motor_want[0], r = g_motor_want[1];
    if (g_motor_valid && l == g_motor_set[0] && r == g_motor_set[1]) return;
    g_motor_set[0] = l;
    g_motor_set[1] = r;
    g_motor_valid = true;
    static const bool trace = GetEnvironmentVariableW(L"TS2_PADSPUR", nullptr, 0) > 0;
    if (trace || g_pad_scripted) log("[pad] Takt %u: Motoren links %u rechts %u\n", g_frame, l, r);
    if (g_pad_scripted || !g_pad_present) return;
    XINPUT_VIBRATION v{l, r};
    XInputSetState(g_pad_port, &v);
}

void motors_off_at_exit() {
    if (g_pad_scripted || !g_motor_valid || (!g_motor_set[0] && !g_motor_set[1])) return;
    XINPUT_VIBRATION v{};
    XInputSetState(g_pad_port, &v);
}

}  // namespace

void input_init(HWND wnd, void* obj, const std::filesystem::path& ini) {
    g_wnd = wnd;
    g_obj = obj;
    const std::wstring p = ini.wstring();
    g_fwd = default_vk(L"vorwaerts")[0];
    g_back = default_vk(L"rueckwaerts")[0];
    g_left = default_vk(L"links")[0];
    g_right = default_vk(L"rechts")[0];
    for (auto& k : g_keys) std::copy(default_vk(k.name), default_vk(k.name) + 3, k.vk);
    const bool have_keys = GetPrivateProfileIntW(L"tasten", L"vorwaerts", -1, p.c_str()) != UINT(-1);
    if (have_keys && int(GetPrivateProfileIntW(L"tasten", L"belegung", 1, p.c_str())) < kBindingVersion) {
        // Abschnitt aus einer früheren Fassung: durch die neue Belegung
        // ersetzen; die Lauftasten bleiben, wie sie dort stehen.
        // Format für WritePrivateProfileSection: "a=1\0b=2\0\0".
        std::wstring sec;
        auto line = [&](const std::wstring& s) {
            sec += s;
            sec.push_back(L'\0');
        };
        auto keep = [&](const wchar_t* key, int def) {
            line(std::wstring(key) + L"=" + std::to_wstring(GetPrivateProfileIntW(L"tasten", key, def, p.c_str())));
        };
        line(L"belegung=" + std::to_wstring(kBindingVersion));
        keep(L"vorwaerts", g_fwd);
        keep(L"rueckwaerts", g_back);
        keep(L"links", g_left);
        keep(L"rechts", g_right);
        for (auto& k : g_keys) line(std::wstring(k.name) + L"=" + vk_list(k.vk));
        sec.push_back(L'\0');
        WritePrivateProfileSectionW(L"tasten", sec.data(), p.c_str());
        log("ts2.ini: [tasten] auf die neue Belegung umgestellt\n");
    }
    g_fwd = int(GetPrivateProfileIntW(L"tasten", L"vorwaerts", g_fwd, p.c_str()));
    g_back = int(GetPrivateProfileIntW(L"tasten", L"rueckwaerts", g_back, p.c_str()));
    g_left = int(GetPrivateProfileIntW(L"tasten", L"links", g_left, p.c_str()));
    g_right = int(GetPrivateProfileIntW(L"tasten", L"rechts", g_right, p.c_str()));
    for (auto& k : g_keys) {
        wchar_t buf[64];
        GetPrivateProfileStringW(L"tasten", k.name, vk_list(k.vk).c_str(), buf, 64, p.c_str());
        int v[3] = {};
        swscanf(buf, L"%d,%d,%d", &v[0], &v[1], &v[2]);
        std::copy(v, v + 3, k.vk);
    }
    wchar_t buf[32];
    GetPrivateProfileStringW(L"maus", L"empfindlichkeit", L"1.0", buf, 32, p.c_str());
    g_mouse_sens = float(_wtof(buf));
    g_mouse_invert = GetPrivateProfileIntW(L"maus", L"umkehren", 0, p.c_str()) != 0;

    RAWINPUTDEVICE rid{0x01, 0x02, 0, wnd};  // Maus
    RegisterRawInputDevices(&rid, 1, sizeof rid);
    call(kConnect);
    pad_load(p);
    pad_script_load();
    mouse_script_load();
    std::atexit(motors_off_at_exit);
    log("Pad: %s, Vibration %s, Totzone %.0f %%, Blick %.2f%s\n", g_pad.on ? "an" : "aus", g_pad.vibration ? "an" : "aus",
        g_pad.deadzone * 100.0f, g_pad.look_sens, g_pad.invert ? ", umgekehrt" : "");
}

bool input_message(const MSG& m) {
    switch (m.message) {
        case WM_INPUT: {
            RAWINPUT ri;
            UINT n = sizeof ri;
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(m.lParam), RID_INPUT, &ri, &n, sizeof(RAWINPUTHEADER)) != UINT(-1) &&
                ri.header.dwType == RIM_TYPEMOUSE) {
                if (!(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) && focused()) {
                    g_mdx += float(ri.data.mouse.lLastX);
                    g_mdy += float(ri.data.mouse.lLastY);
                    if (ri.data.mouse.lLastX || ri.data.mouse.lLastY) g_use_pad = false;
                }
                if (ri.data.mouse.usButtonFlags & RI_MOUSE_WHEEL) {
                    g_wheel += SHORT(ri.data.mouse.usButtonData) > 0 ? 1 : -1;
                    g_use_pad = false;
                }
            }
            return false;  // DefWindowProc soll aufräumen
        }
        case WM_KILLFOCUS:
            capture(false);
            return false;
    }
    return false;
}

void input_script(const wchar_t* spec) {
    // "takt:bits:anzahl,takt:bits:anzahl", bits hexadezimal
    std::wstring s = spec;
    size_t i = 0;
    while (i < s.size()) {
        size_t e = s.find(L',', i);
        std::wstring part = s.substr(i, e == std::wstring::npos ? std::wstring::npos : e - i);
        Scripted sc{};
        if (swscanf(part.c_str(), L"%u:%x:%u", &sc.from, &sc.bits, &sc.count) == 3) g_script.push_back(sc);
        if (e == std::wstring::npos) break;
        i = e + 1;
    }
    log("Eingabe-Skript: %zu Einträge\n", g_script.size());
}

void input_frame() {
    ++g_frame;
    if (!g_mouse_script.empty()) {
        const MouseStep* cur = nullptr;
        for (auto& st : g_mouse_script)
            if (st.frame <= g_frame) cur = &st;
        if (cur) g_mdx += cur->dx, g_mdy += cur->dy, g_use_pad = false;
    }
    g_last_look[0] = g_last_look[1] = 0;
    uint32_t bits = 0, pad_host = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0;

    // Pad (XInput, das erste verbundene; [pad] an=0 schaltet es ab). Ohne
    // Fokus zählt das echte Pad nicht.
    XINPUT_STATE xs{};
    DWORD port = 0;
    const bool f = focused();
    const bool pad = g_pad.on && pad_state(&xs, &port) && (f || g_pad_scripted);
    g_pad_present = pad;
    if (pad) {
        g_pad_port = port;
        const auto& g = xs.Gamepad;
        static DWORD last_packet = 0;
        if (xs.dwPacketNumber != last_packet) {
            last_packet = xs.dwPacketNumber;
            if (g.wButtons || std::abs(g.sThumbLX) > 8000 || std::abs(g.sThumbLY) > 8000 || std::abs(g.sThumbRX) > 8000 ||
                std::abs(g.sThumbRY) > 8000 || g.bLeftTrigger > 30 || g.bRightTrigger > 30)
                g_use_pad = true;
        }
    }

    // Mehrspieler (multiplayer.cpp): nur Pads; Spieler 1 hat Pad 1, auch
    // ohne dass es zuerst benutzt wurde; Tastatur und Maus zählen nicht
    const bool mp = mp_active();
    if (mp) g_use_pad = true;

    if (g_use_pad && pad) {
        const auto& g = xs.Gamepad;
        const PadOutput o = translate(g, g_p1);
        bits = o.bits;
        pad_host = o.host;
        if (mp) pad_host &= ~kHostFreecam;  // freie Kamera nur allein (eine Kamera)
        lx = o.lx, ly = o.ly, rx = o.rx, ry = o.ry;
        bits |= g_pad_raw;
        capture(false);
        if (g_mode != 0) call(kSetMode, 0), g_mode = 0;
        // Blick: TS2s eigenes Pad-Zielen (Port 0), skaliert
        rx *= g_pad.look_sens;
        ry *= g_pad.look_sens * (g_pad.invert ? -1.0f : 1.0f);
        if (g_freecam) {
            // freie Kamera: linker Stick fliegen, rechter blicken (quadratisch
            // für feines Zielen), Steuerkreuz hoch/runter oder RT/LT steigen/
            // sinken, LB/RB schneller
            const float up = float((g.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0) -
                             float((g.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0) +
                             (float(g.bRightTrigger) - float(g.bLeftTrigger)) / 255.0f;
            g_fc = {lx, ly, rx * std::fabs(rx) * 25.0f, -ry * std::fabs(ry) * 25.0f, std::clamp(up, -1.0f, 1.0f),
                    (g.wButtons & (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER)) != 0};
            g_fc.use = (bits & kActivate) != 0;  // Benutzen: Spieler an die Kamera (features.cpp)
            lx = ly = rx = ry = 0;
            pad_host &= kHostFreecam;
        }
        call(kSetLeftX, lx);
        call(kSetLeftY, ly);
        call(kLookX, rx);
        call(kLookY, ry);
    } else if (mp) {
        // Mehrspieler ohne Pad 1 (oder ohne Fokus): Spieler 1 steht
        g_p1.aim_latched = false;
        g_wheel = 0;
        capture(false);
        if (g_mode != 0) call(kSetMode, 0), g_mode = 0;
        call(kSetLeftX, 0.0f);
        call(kSetLeftY, 0.0f);
        call(kLookX, 0.0f);
        call(kLookY, 0.0f);
    } else {
        // Menümaus: Zeiger und Klicks im Menü (menumouse.cpp); Maustasten
        // gehen dann nicht als Feuern/Zielen an TS2
        bool menu_active = false;
        bits |= menumouse_frame(g_wnd, f, &menu_active);
        if (f) {
            for (auto& k : g_keys)
                for (int vk : k.vk)
                    if (down(vk) && !(menu_active && vk >= VK_LBUTTON && vk <= VK_XBUTTON2 && vk != VK_CANCEL))
                        bits |= k.bit;
            lx = float(down(g_right)) - float(down(g_left));
            ly = float(down(g_fwd)) - float(down(g_back));
            if (bits || lx || ly) g_use_pad = false;
        }
        g_p1.aim_latched = false;
        // Mausrad: beim Zielen (Zielen-Taste gehalten) die Zoomstufen,
        // sonst Waffenwechsel
        if (bits & kZoom) {
            if (g_wheel > 0) bits |= kZoomIn;
            if (g_wheel < 0) bits |= kZoomOut;
        } else {
            if (g_wheel > 0) bits |= kPrevWeapon;
            if (g_wheel < 0) bits |= kNextWeapon;
        }
        g_wheel = 0;
        capture(f && !menu_active);
        if (g_mode != 1) call(kSetMode, 1), g_mode = 1;
        const float s = g_mouse_sens;
        if (g_freecam) {
            g_fc = {lx, ly, g_mdx * s, g_mdy * s * (g_mouse_invert ? -1.0f : 1.0f),
                    float(f && down(VK_SPACE)) - float(f && down('C')), f && down(VK_SHIFT)};
            lx = ly = 0;
            g_mdx = g_mdy = 0;
        }
        call(kSetLeftX, lx);
        call(kSetLeftY, ly);
        // Maus 1:1 (oben): durch die Bildzeit, Nick wie Gier
        mouse_rebase();
        const float k = mouse_time_scale();
        call(kLookX, g_mdx * s * k);
        call(kLookY, -(g_mdy * s * kPitchMatch * k) * (g_mouse_invert ? -1.0f : 1.0f));
        g_last_look[0] = g_mdx * s;
        g_last_look[1] = g_mdy * s;
    }
    g_mdx = g_mdy = 0;

    uint32_t scripted = 0;
    for (auto& sc : g_script)
        if (g_frame >= sc.from && g_frame < sc.from + sc.count) scripted |= sc.bits;
    g_host_bits = (scripted & kHostBits) | pad_host;
    if (g_freecam) {
        // Skript: vorwärts und nach rechts drehen bewegen die Kamera
        if (g_host_bits & kScriptForward) g_fc.move_y = 1.0f;
        if (g_host_bits & kHostMouseRight) g_fc.look_x += 20.0f * g_mouse_sens;
        if (scripted & kActivate) g_fc.use = true;  // Skript: Benutzen wie am Pad
        g_host_bits &= kHostFreecam;  // kein Sprung, kein Rennen für den Spieler
        bits &= kStart;               // Pause bleibt erreichbar
    } else {
        if (g_host_bits & kScriptForward) call(kSetLeftY, 1.0f);
        if ((g_host_bits & kHostMouseRight) && g_mode == 1) {
            call(kLookX, 20.0f * g_mouse_sens * mouse_time_scale());
            g_last_look[0] += 20.0f * g_mouse_sens;
        }
        bits |= scripted & ~kHostBits;
        // Skript-Mausrad: wie das echte (beim Zielen Zoomstufen)
        if (g_mode == 1 && (g_host_bits & (kHostWheelUp | kHostWheelDown))) {
            const bool up = (g_host_bits & kHostWheelUp) != 0;
            bits |= (bits & kZoom) ? (up ? kZoomIn : kZoomOut) : (up ? kPrevWeapon : kNextWeapon);
        }
    }
    call(kSetButtons, int32_t(bits));
    motors_apply(g_use_pad && pad);
    mp_frame();  // Spieler 2–4
}

void input_rumble(int small_motor, int large_motor) {
    // Mehrspieler: features.cpp liest das Pad des zuletzt bewegten Spielers;
    // Spieler 1 bekommt seine Motoren dann aus multiplayer.cpp
    if (mp_active()) return;
    input_rumble_p1(small_motor, large_motor);
}

void input_rumble_p1(int small_motor, int large_motor) {
    // TS2: je 0..127; XInput: links der große (tiefe), rechts der kleine Motor
    auto scale = [](int v) {
        const float f = std::clamp(float(v) / 127.0f * g_pad.vib_strength, 0.0f, 1.0f);
        return WORD(f * 65535.0f + 0.5f);
    };
    g_motor_want[0] = scale(large_motor);
    g_motor_want[1] = scale(small_motor);
}

uint32_t input_host_bits() { return g_host_bits; }

void input_set_freecam(bool on) {
    g_freecam = on;
    g_p1.aim_latched = false;
    g_fc = {};
}

FreecamInput input_freecam() { return g_fc; }

void input_reclip() {
    if (!g_captured) return;
    RECT r;
    GetClientRect(g_wnd, &r);
    MapWindowPoints(g_wnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
    ClipCursor(&r);
}
bool input_uses_mouse() { return g_mode == 1; }

PadOutput input_translate_pad(const XINPUT_GAMEPAD& g, int slot) {
    PadOutput o = translate(g, g_tracks[std::clamp(slot, 1, kMaxPads - 1)]);
    o.rx *= g_pad.look_sens;
    o.ry *= g_pad.look_sens * (g_pad.invert ? -1.0f : 1.0f);
    return o;
}

void input_reset_pad(int slot) { g_tracks[std::clamp(slot, 1, kMaxPads - 1)] = PadTrack{}; }

bool input_pad_on() { return g_pad.on; }
bool input_pad_vibration(float* strength) {
    *strength = g_pad.vib_strength;
    return g_pad.vibration;
}
int input_pad1_user() { return g_pad_present && !g_pad_scripted ? int(g_pad_port) : -1; }
bool input_pad1_present() { return g_pad.on && g_pad_present; }
bool input_pad_scripted() { return g_pad_scripted; }
bool input_focused() { return focused(); }
uint32_t input_frame_number() { return g_frame; }
void input_attach_image(const Ts2Image& img) { g_img = &img; }
void input_last_mouse(float* dx, float* dy) {
    *dx = g_last_look[0];
    *dy = g_last_look[1];
}

}  // namespace host
