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
constexpr uint32_t kScriptForward = kHostForward, kHostBits = kHostJump | kHostForward | kHostRun | kHostMouseRight;
uint32_t g_host_bits = 0;

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

float stick(SHORT v, SHORT dead) {
    if (std::abs(v) <= dead) return 0.0f;
    float f = (std::abs(v) - dead) / float(32767 - dead);
    return std::copysign(std::fmin(f, 1.0f), float(v));
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
    uint32_t bits = 0, pad_host = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0;

    // Pad (XInput, das erste verbundene)
    XINPUT_STATE xs{};
    bool pad = false;
    for (DWORD i = 0; i < 4 && !pad; ++i) pad = XInputGetState(i, &xs) == ERROR_SUCCESS;
    if (pad) {
        const auto& g = xs.Gamepad;
        static DWORD last_packet = 0;
        if (xs.dwPacketNumber != last_packet) {
            last_packet = xs.dwPacketNumber;
            if (g.wButtons || std::abs(g.sThumbLX) > 8000 || std::abs(g.sThumbLY) > 8000 || std::abs(g.sThumbRX) > 8000 ||
                std::abs(g.sThumbRY) > 8000 || g.bLeftTrigger > 30 || g.bRightTrigger > 30)
                g_use_pad = true;
        }
    }

    const bool f = focused();
    if (g_use_pad && pad) {
        const auto& g = xs.Gamepad;
        lx = stick(g.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        ly = stick(g.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        rx = stick(g.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        ry = stick(g.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        // Xbox-Belegung auf TS2s Konsolen-Bits
        const WORD b = g.wButtons;
        if (b & XINPUT_GAMEPAD_A) bits |= kCross;
        if (b & XINPUT_GAMEPAD_B) bits |= kCircle;
        if (b & XINPUT_GAMEPAD_X) bits |= kSquare;
        if (b & XINPUT_GAMEPAD_Y) bits |= kTriangle;
        // Schultertasten wie Black/White im Xbox-Fork: Rennen und Sprung,
        // nicht an TS2
        if (b & XINPUT_GAMEPAD_LEFT_SHOULDER) pad_host |= kHostRun;
        if (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) pad_host |= kHostJump;
        if (g.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) bits |= kL1;
        if (g.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) bits |= kR1;
        if (b & (XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK)) bits |= kStart;
        if (b & XINPUT_GAMEPAD_DPAD_UP) bits |= kUp;
        if (b & XINPUT_GAMEPAD_DPAD_DOWN) bits |= kDown;
        if (b & XINPUT_GAMEPAD_DPAD_LEFT) bits |= kLeft;
        if (b & XINPUT_GAMEPAD_DPAD_RIGHT) bits |= kRight;
        capture(false);
        if (g_mode != 0) call(kSetMode, 0), g_mode = 0;
        call(kSetLeftX, lx);
        call(kSetLeftY, ly);
        call(kLookX, rx);
        call(kLookY, ry);
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
        if (g_wheel > 0) bits |= kPrevWeapon;
        if (g_wheel < 0) bits |= kNextWeapon;
        g_wheel = 0;
        capture(f && !menu_active);
        if (g_mode != 1) call(kSetMode, 1), g_mode = 1;
        call(kSetLeftX, lx);
        call(kSetLeftY, ly);
        const float s = g_mouse_sens;
        call(kLookX, g_mdx * s);
        call(kLookY, -(g_mdy * s) * (g_mouse_invert ? -1.0f : 1.0f));
    }
    g_mdx = g_mdy = 0;

    uint32_t scripted = 0;
    for (auto& sc : g_script)
        if (g_frame >= sc.from && g_frame < sc.from + sc.count) scripted |= sc.bits;
    g_host_bits = (scripted & kHostBits) | pad_host;
    if (g_host_bits & kScriptForward) call(kSetLeftY, 1.0f);
    if ((g_host_bits & kHostMouseRight) && g_mode == 1) call(kLookX, 20.0f * g_mouse_sens);
    bits |= scripted & ~kHostBits;
    call(kSetButtons, int32_t(bits));
}

uint32_t input_host_bits() { return g_host_bits; }
bool input_uses_mouse() { return g_mode == 1; }

}  // namespace host
