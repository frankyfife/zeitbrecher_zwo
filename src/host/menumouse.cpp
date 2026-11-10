#include "menumouse.h"

#include <cstdio>
#include <string>
#include <vector>

#include "hf2.h"
#include "hooks.h"
#include "log.h"

namespace host {

namespace {

// Tastenbits wie in input.cpp (Homefronts Übersetzer 0x14015F880)
constexpr uint32_t kUp = 0x1000, kDown = 0x4000, kOk = 0x40, kBack = 0x20;
constexpr float kUiWidth = 1920.0f, kUiHeight = 1080.0f;

using UpdateFn = void (*)(uint8_t* menu);
UpdateFn g_update = nullptr;
bool g_on = false;
uint8_t* g_seen = nullptr;  // Menü, das im letzten Takt Eingaben nahm

template <class T>
T& at(uint8_t* p, uint32_t off) {
    return *reinterpret_cast<T*>(p + off);
}

bool takes_input(uint8_t* m) {
    uint8_t* page = at<uint8_t*>(m, 0x08);
    return page && !(at<uint32_t>(page, 0x10) & 2) && (at<uint32_t>(m, 0x14) & 6) == 6 &&
           at<int32_t>(m, 0x3A8) > 0 && at<int32_t>(m, 0x3B0) >= -1;
}

// an Stelle des Aufrufs 0x1410E05AB (Schleife über die Menüs einer Seite)
void update_hook(uint8_t* m) {
    if (g_on && m && takes_input(m)) g_seen = m;
    g_update(m);
}

// Welche Zeile liegt unter dem Zeiger (Oberflächen-Koordinaten)? -1: keine
// oder eine, die nicht gewählt werden kann. Wie ts2_menu_hit im Fork, ohne
// Spalten und ohne das Pausenmenü (Einträge ohne Höhe).
int hit_row(uint8_t* m, float gx, float gy) {
    const int x0 = at<int32_t>(m, 0x3C), y0 = at<int32_t>(m, 0x40);
    const int body0 = at<int32_t>(m, 0x2C), body1 = at<int32_t>(m, 0x30);
    float scroll = at<float>(m, 0x5C);
    if (!(scroll > -100000.0f && scroll < 100000.0f)) scroll = 0.0f;
    int row = -1;
    uint8_t* it = at<uint8_t*>(m, 0x3A0);
    for (int n = 0; n < 256 && it; ++n, it = at<uint8_t*>(it, 0x08)) {
        if (at<int32_t>(it, 0x28) == 0) continue;  // keine eigene Zeile
        ++row;
        const int ix = at<int32_t>(it, 0x9C), iy = at<int32_t>(it, 0xA0);
        const int iw = at<int32_t>(it, 0xA4), ih = at<int32_t>(it, 0xA8);
        const uint32_t flags = at<uint32_t>(it, 0x44);
        if (ih <= 0) {
            // 3D-Menüs (Pause, Beenden?, Tod, Levelende): Einträge ohne Höhe,
            // ihre Lage ein Float bei +0x98 (Xbox +0x60). Ihre Breite ist 1920
            // oder 0 (z. B. "Beenden?"); früher schied 0 hier aus, und die Maus
            // ging in diesen Menüs nicht. Am Bild gemessen (OPTIONS 0,49 → 278, Continue 0,22 → 446,
            // Quit 0,04 → 543): y = 571 − 593 · v, ±6 px.
            const float v = at<float>(it, 0x98);
            const float cy = 571.0f - 593.0f * v;
            if (v > -2.0f && v < 2.0f && gy >= cy - 45.0f && gy < cy + 45.0f && gx >= 280.0f && gx < 1640.0f)
                return (flags & 8) ? -1 : row;
            continue;
        }
        if (iw <= 0) continue;
        const float x = float(x0 + ix);
        float y;
        bool hit;
        if (flags & 0x100) {
            y = float(y0 + body0 + iy) - scroll;
            hit = gy >= float(y0 + body0) && (body1 <= body0 || gy < float(y0 + body1));
        } else {
            y = float(y0 + iy);
            hit = true;
        }
        hit = hit && gx >= x && gx < x + float(iw) && gy >= y && gy < y + float(ih);
        if (hit) return (flags & 8) ? -1 : row;
    }
    return -1;
}

// ---- Drücke für den Spieler: gehalten, dann eine Pause losgelassen
uint32_t g_press = 0;
int g_hold = 0, g_gap = 0;
bool busy() { return g_hold > 0 || g_gap > 0; }
void press(uint32_t bits) {
    g_press = bits;
    g_gap = 0;
    g_hold = (bits & (kOk | kBack)) ? 1 : 2;  // OK/Zurück nur einen Takt
}

int g_target = -1, g_target_presses = 0;
bool g_click_pending = false;
float g_last_x = -1, g_last_y = -1;
bool g_l_was = false, g_r_was = false;
uint32_t g_tick = 0;

struct ScriptStep {
    uint32_t tick;
    float x, y;
    char click;  // 0, 'l', 'r'
};
std::vector<ScriptStep> g_script;

}  // namespace

void menumouse_install(const Ts2Image& img, bool enabled) {
    g_on = enabled;
    if (!enabled) return;
    g_update = reinterpret_cast<UpdateFn>(
        hook_call(img, hf2::kMenuUpdateCall, hf2::kMenuUpdate, reinterpret_cast<const void*>(&update_hook)));
    if (!g_update) {
        g_on = false;
        return;
    }
    log("Menümaus an\n");
}

void menumouse_script(const wchar_t* spec) {
    std::wstring s = spec;
    size_t i = 0;
    while (i < s.size()) {
        size_t e = s.find(L';', i);
        std::wstring part = s.substr(i, e == std::wstring::npos ? std::wstring::npos : e - i);
        ScriptStep st{};
        wchar_t c = 0;
        const int n = swscanf(part.c_str(), L"%u:%f,%f,%lc", &st.tick, &st.x, &st.y, &c);
        if (n >= 3) {
            st.click = n == 4 ? char(c) : 0;
            g_script.push_back(st);
        }
        if (e == std::wstring::npos) break;
        i = e + 1;
    }
    log("Menümaus-Skript: %zu Schritte\n", g_script.size());
}

uint32_t menumouse_frame(HWND wnd, bool focused, bool* menu_active) {
    ++g_tick;
    // gehaltene Drücke laufen aus
    if (g_hold > 0) {
        if (--g_hold == 0) g_gap = 2;
    } else if (g_gap > 0) {
        --g_gap;
    }
    uint8_t* m = g_seen;
    g_seen = nullptr;
    *menu_active = m != nullptr;
    // TS2_MENUSPUR=1: jedes neue Menü mit Lage, Zeilen und Einträgen protokollieren
    static const bool trace = [] {
        wchar_t v[4] = {};
        return GetEnvironmentVariableW(L"TS2_MENUSPUR", v, 4) > 0;
    }();
    static uint8_t* traced = nullptr;
    if (trace && m && (m != traced || g_tick % 50 == 0)) {
        traced = m;
        log("[menuemaus] Takt %u Menü %p: Lage %d,%d Größe %d×%d Rumpf %d..%d Scroll %.1f Zeilen %d gewählt %d Flags %X\n",
            g_tick, m, at<int32_t>(m, 0x3C), at<int32_t>(m, 0x40), at<int32_t>(m, 0x4C), at<int32_t>(m, 0x50),
            at<int32_t>(m, 0x2C), at<int32_t>(m, 0x30), at<float>(m, 0x5C), at<int32_t>(m, 0x3A8),
            at<int32_t>(m, 0x3B0), at<uint32_t>(m, 0x14));
        uint8_t* it = at<uint8_t*>(m, 0x3A0);
        for (int n = 0; n < 24 && it; ++n, it = at<uint8_t*>(it, 0x08))
            log("[menuemaus]   %d: Zeile %d Flags %X Rechteck %d,%d %d×%d  +0x98 %.3f\n", n, at<int32_t>(it, 0x28),
                at<uint32_t>(it, 0x44), at<int32_t>(it, 0x9C), at<int32_t>(it, 0xA0), at<int32_t>(it, 0xA4),
                at<int32_t>(it, 0xA8), at<float>(it, 0x98));
    }
    if (!m) {
        g_target = -1;
        g_click_pending = false;
        return g_hold > 0 ? g_press : 0;
    }

    // Zeiger und Klicks: aus dem Testskript oder von Windows
    float gx = -1, gy = -1;
    bool left = false, right = false;
    if (!g_script.empty()) {
        for (auto& st : g_script)
            if (st.tick <= g_tick) {
                gx = st.x;
                gy = st.y;
                if (st.tick == g_tick) left = st.click == 'l', right = st.click == 'r';
            }
    } else if (focused) {
        POINT p;
        RECT r;
        GetCursorPos(&p);
        ScreenToClient(wnd, &p);
        GetClientRect(wnd, &r);
        if (r.right > 0 && r.bottom > 0 && p.x >= 0 && p.y >= 0 && p.x < r.right && p.y < r.bottom) {
            gx = float(p.x) * kUiWidth / float(r.right);
            gy = float(p.y) * kUiHeight / float(r.bottom);
            const bool l = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, rr = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
            left = l && !g_l_was;
            right = rr && !g_r_was;
            g_l_was = l;
            g_r_was = rr;
        }
    }
    const bool moved = gx >= 0 && (gx != g_last_x || gy != g_last_y);
    g_last_x = gx;
    g_last_y = gy;
    const int hit = gx >= 0 ? hit_row(m, gx, gy) : -1;
    const int sel = at<int32_t>(m, 0x3B0);
    const int rows = at<int32_t>(m, 0x3A8);

    // Ein Menü ohne gewählte Zeile (das Briefing) wartet nur auf eine
    // Bestätigung: ein Klick in seine Fläche ist OK (Fork: "Controller wählen").
    if (sel == -1) {
        const float x0 = float(at<int32_t>(m, 0x3C)), y0 = float(at<int32_t>(m, 0x40));
        const float w = float(at<int32_t>(m, 0x4C)), h = float(at<int32_t>(m, 0x50));
        if (left && !busy() && gx >= x0 && gx < x0 + w && gy >= y0 && gy < y0 + h) press(kOk);
        else if (right && !busy()) press(kBack);
        return g_hold > 0 ? g_press : 0;
    }
    // Klick zuerst (eine Hand bewegt die Maus beim Klicken immer etwas)
    if (left && hit >= 0) {
        if (hit == sel && !busy())
            press(kOk);
        else
            g_target = hit, g_target_presses = 0, g_click_pending = true;
    } else if (right && !busy()) {
        press(kBack);
    } else if (moved && hit >= 0 && hit != g_target) {
        // nur Bewegung wählt eine Zeile: ein ruhender Zeiger kämpft nie gegen die Tasten
        g_target = hit;
        g_target_presses = 0;
    }
    // der Zielzeile nachjagen, ein Druck nach dem anderen
    if (g_target >= 0 && !busy()) {
        if (sel == g_target) {
            g_target = -1;
            if (g_click_pending) press(kOk);
            g_click_pending = false;
        } else if (g_target_presses < rows) {
            press(g_target > sel ? kDown : kUp);
            ++g_target_presses;
        } else {
            g_target = -1;
            g_click_pending = false;
        }
    }
    return g_hold > 0 ? g_press : 0;
}

}  // namespace host
