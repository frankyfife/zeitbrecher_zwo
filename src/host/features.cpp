#include "features.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#include "hf2.h"
#include "hooks.h"
#include "input.h"
#include "log.h"
#include "multiplayer.h"

namespace host {

Settings load_settings(const std::filesystem::path& ini) {
    Settings s;
    const std::wstring p = ini.wstring();
    s.width = int(GetPrivateProfileIntW(L"grafik", L"breite", 1920, p.c_str()));
    s.height = int(GetPrivateProfileIntW(L"grafik", L"hoehe", 1080, p.c_str()));
    s.fullscreen = GetPrivateProfileIntW(L"grafik", L"vollbild", 0, p.c_str()) != 0;
    s.vsync = GetPrivateProfileIntW(L"grafik", L"bildsync", 0, p.c_str()) != 0;
    // 120 Hz: Spieler und Kamera nativ, die übrige Welt rechnet in 60-Hz-Takten
    // (docs/host.md, "120 Hz"); [grafik] takt=120 oder TS2_TAKT=120
    {
        wchar_t v[8] = {};
        const int env = GetEnvironmentVariableW(L"TS2_TAKT", v, 8) > 0 ? _wtoi(v) : 0;
        const int ini_rate = int(GetPrivateProfileIntW(L"grafik", L"takt", 60, p.c_str()));
        s.tick_rate = (env ? env : ini_rate) == 120 ? 120 : 60;
    }
    s.weapon_switch_percent = int(GetPrivateProfileIntW(L"waffen", L"wechseltempo", 100, p.c_str()));
    s.rof_percent = int(GetPrivateProfileIntW(L"waffen", L"feuerrate", 100, p.c_str()));
    if (s.rof_percent < 10 || s.rof_percent > 1000) s.rof_percent = 100;
    for (int w = 0; w < 46; ++w) {
        const std::wstring key = L"feuerrate_" + std::to_wstring(w);
        s.rof_row[w] = int(GetPrivateProfileIntW(L"waffen", key.c_str(), 0, p.c_str()));
    }
    s.jump = GetPrivateProfileIntW(L"bewegung", L"springen", 1, p.c_str()) != 0;
    s.jump_key = int(GetPrivateProfileIntW(L"bewegung", L"sprungtaste", 0x20, p.c_str()));
    wchar_t buf[64];
    GetPrivateProfileStringW(L"bewegung", L"sprunghoehe", L"1.2", buf, 64, p.c_str());
    s.jump_height = float(_wtof(buf));
    GetPrivateProfileStringW(L"maus", L"nickgrenze", L"85", buf, 64, p.c_str());
    s.pitch_limit = std::clamp(float(_wtof(buf)), 40.0f, 89.0f);
    s.run = GetPrivateProfileIntW(L"bewegung", L"rennen", 1, p.c_str()) != 0;
    s.run_key = int(GetPrivateProfileIntW(L"bewegung", L"renntaste", 0xA0, p.c_str()));
    s.run_percent = std::clamp(int(GetPrivateProfileIntW(L"bewegung", L"renntempo", 160, p.c_str())), 100, 300);
    s.run_toggle = GetPrivateProfileIntW(L"bewegung", L"rennen_umschalten", 0, p.c_str()) != 0;
    s.fov_percent = std::clamp(int(GetPrivateProfileIntW(L"grafik", L"sichtfeld", 100, p.c_str())), 70, 150);
    s.freecam = GetPrivateProfileIntW(L"kamera", L"freecam", 1, p.c_str()) != 0;
    s.freecam_key = int(GetPrivateProfileIntW(L"kamera", L"freecam_taste", 0x75, p.c_str()));
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

// ---- Rennen (Xbox-Fork: ts2_run_walk)
// Der Gehcode (Bewegungsmodus 0, 0x1411D9748) bekommt vom Dispatcher
// (Aufruf 0x1411DBE73) zwei Schritte für diesen Takt, das dritte und sechste
// Argument (aus dem linken Stick), und deckelt beide zusammen auf den
// Anteil einer Höchstgeschwindigkeit je Haltung (Spieler +0xCC + 4 · Haltung,
// Haltung +0x168; Xbox +0xC0/+0x15C). Beim Rennen werden beide Schritte und
// der Deckel für diesen Aufruf mit dem Tempo multipliziert, der Deckel danach
// zurückgesetzt: Kollision, Steigungen und der Rest des Gehens sehen nur
// einen schnelleren Stick. Nur für Spieler 1.
using WalkFn = void (*)(void* ctx, float* a, float step1, float* b, float* c, float step2);
WalkFn g_walk = nullptr;

// Sichtfeld (Xbox-Fork: ts2_fov): jedes fov, das das Spiel der Kamera-
// Einrichtung gibt, wird skaliert, auch das des Zielfernrohrs (der Zoom
// bleibt im Verhältnis des Spiels). Projektion und Ausblend-Halbbreiten
// folgen daraus, nichts verschwindet am Rand. Das Spiel merkt sich sein
// eigenes fov (+0x350), das Skalieren schaukelt sich also nicht auf.
using CameraSetupFn = void (*)(float near_z, float far_z, float aspect, float fov_deg);
CameraSetupFn g_camera_setup = nullptr;
float g_fov_scale = 1.0f;

void camera_setup_fov(float near_z, float far_z, float aspect, float fov_deg) {
    g_camera_setup(near_z, far_z, aspect, std::min(fov_deg * g_fov_scale, 170.0f));
}

// Freie Kamera (Xbox-Fork: ts2_freecam): Taste (F6) löst die Kamera vom
// Spieler und fliegt sie; dieselbe Taste bringt sie zurück. Das Spiel läuft
// weiter, der Spieler steht. Kameramodus 8 baut die Sicht aus Position und
// Blickpunkt (Globale); beide schreibt features_tick je Takt. Den Raum, aus
// dem das Level gezeichnet wird, nimmt Modus 8 sonst vom Spieler; ein Stück
// an 0x1410828EE nimmt stattdessen g_fc_room, solange er nicht 0 ist
// (Raumsuche 0x1411BFA08 an der Kamera-Position, wie die Flugkamera Modus 6).
//   WASD bewegen, Maus blickt, Leertaste/C steigen/sinken, Umschalt schneller
//   (Pad: [pad] freecam = L3+R3, Sticks, Steuerkreuz oder RT/LT, LB/RB schneller)
using FindRoomFn = int32_t (*)(const float* pos, float one, float arg);
int32_t g_fc_room = 0;
bool g_fc_ready = false;
const Ts2Image* g_img = nullptr;
float g_run_factor = 1.0f;
bool g_running = false;
int g_frame_no = 0;  // Bilder seit dem Start (Proben)

void run_walk(void* ctx, float* a, float step1, float* b, float* c, float step2) {
    uint8_t* now = *g_img->va<uint8_t*>(hf2::kPlayerNow);
    float* cap = nullptr;
    float cap_was = 0.0f;
    if (g_running && now && now == player1(*g_img)) {
        const int32_t stance = *reinterpret_cast<int32_t*>(now + hf2::kPlayerStance);
        if (stance >= 0 && stance < 4) {
            cap = reinterpret_cast<float*>(now + hf2::kPlayerTopSpeed + 4 * stance);
            cap_was = *cap;
            *cap *= g_run_factor;
        }
        step1 *= g_run_factor;
        step2 *= g_run_factor;
    }
    // TS2_GEHPROBE=1: Bild, Bildzeit und Schritte der ersten 40 Gehaufrufe von Spieler 1 mit Schritt
    static const bool probe = [] {
        wchar_t v[4] = {};
        return GetEnvironmentVariableW(L"TS2_GEHPROBE", v, 4) > 0;
    }();
    static int logged = 0;
    if (probe && now && now == player1(*g_img) && (step1 != 0.0f || step2 != 0.0f) && logged < 40) {
        ++logged;
        log("[gehen] Bild %d: Bildzeit %.2f, Takte %d, Schritte %.4f %.4f\n", g_frame_no,
            *g_img->va<float>(hf2::kFrameTicks), *g_img->va<int32_t>(0x14259CBDC), step1, step2);
    }
    g_walk(ctx, a, step1, b, c, step2);
    if (cap) *cap = cap_was;
}

}  // namespace

// Feuerrate je Waffe (Xbox-Fork: ts2_rof_apply). Die Waffentabelle
// (0x14250C2A8, 46 Zeilen à 0x240, dahinter −1; Aufbau wie auf der Xbox,
// Werte wie die USA-Fassung) gibt jedem der beiden Feuermodi (0xB0 ab +0x58)
// den Abstand zwischen zwei Schüssen in Takten bei +0x10 und +0x14. Die
// Abstände werden einmal durch das Tempo geteilt, nie unter einen Takt. Die
// Tabelle gilt für alle, auch die KI.
void apply_rate_of_fire(const Ts2Image& img, const Settings& s) {
    int changed = 0;
    const auto* row0 = reinterpret_cast<const int32_t*>(img.va(hf2::kWeaponTable) + 0x68);
    const int32_t before[2] = {row0[0], row0[1]};
    for (int w = 0; w < hf2::kWeaponRows; ++w) {
        int pct = s.rof_percent;
        if (s.rof_row[w] >= 10 && s.rof_row[w] <= 1000) pct = s.rof_percent * s.rof_row[w] / 100;
        if (pct == 100 || pct <= 0) continue;
        uint8_t* row = img.va(hf2::kWeaponTable) + size_t(w) * hf2::kWeaponRowSize;
        for (int m = 0; m < 2; ++m)
            for (int k = 0x10; k <= 0x14; k += 4) {
                auto* t = reinterpret_cast<int32_t*>(row + 0x58 + m * 0xB0 + k);
                if (*t <= 0 || *t > 10000) continue;
                const int n = (*t * 100 + pct / 2) / pct;
                *t = n < 1 ? 1 : n;
            }
        ++changed;
    }
    if (changed)
        log("Feuerrate: %d Waffe(n) geändert (alle %d %%; Zeile 0: %d/%d -> %d/%d Takte)\n", changed, s.rof_percent,
            before[0], before[1], row0[0], row0[1]);
}

// Waffenwechsel: TS2 liest das Tempo (20/7 je Sekunde) an genau einer Stelle
// (0x14112D4A0); wie im Xbox-Fork wird die Konstante einmal skaliert.
void features_install(const Ts2Image& img, const Settings& s) {
    g_img = &img;
    if (s.run) {
        g_run_factor = float(s.run_percent) / 100.0f;
        g_walk = reinterpret_cast<WalkFn>(hook_call(img, hf2::kWalkCall, hf2::kWalk, reinterpret_cast<const void*>(&run_walk)));
        if (g_walk) log("Rennen: %d %%\n", s.run_percent);
    }
    if (s.fov_percent != 100) {
        g_fov_scale = float(s.fov_percent) / 100.0f;
        const auto fn = reinterpret_cast<const void*>(&camera_setup_fov);
        auto* a = hook_call(img, hf2::kCameraSetupCallInit, hf2::kCameraSetup, fn);
        auto* b = hook_call(img, hf2::kCameraSetupCallFrame, hf2::kCameraSetup, fn);
        g_camera_setup = reinterpret_cast<CameraSetupFn>(a ? a : b);
        log("Sichtfeld: %d %% (%s)\n", s.fov_percent, a && b ? "beide Aufrufe" : "FEHLER: Aufruf nicht gefunden");
    }
    if (s.freecam) {
        // mov rcx,&g_fc_room; mov ecx,[rcx]; test ecx,ecx; jnz ret; mov ecx,[r12+0xD78]; ret
        static const uint8_t expected[] = {0x41, 0x8B, 0x8C, 0x24, 0x78, 0x0D, 0x00, 0x00};
        uint8_t stub[2 + 8 + 2 + 2 + 2 + 8 + 1] = {0x48, 0xB9};
        const uint64_t room_addr = reinterpret_cast<uint64_t>(&g_fc_room);
        std::memcpy(stub + 2, &room_addr, 8);
        const uint8_t rest[] = {0x8B, 0x09, 0x85, 0xC9, 0x75, 0x08, 0x41, 0x8B, 0x8C, 0x24, 0x78, 0x0D, 0x00, 0x00, 0xC3};
        std::memcpy(stub + 10, rest, sizeof rest);
        g_fc_ready = patch_with_call(img, hf2::kCamMode8Room, expected, sizeof expected, stub, sizeof stub);
        log("Freie Kamera: Taste %d%s\n", s.freecam_key, g_fc_ready ? "" : " (FEHLER: Stelle 0x1410828EE unerwartet, aus)");
    }
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
    apply_rate_of_fire(img, s);
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
    ++g_frame_no;
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
    // TS2_VIEWPROBE=1: Bildausschnitt *0x143F0F508 und Kamera-Halbbreiten alle
    // 120 Takte (Pausenmenü: Texte gegen Balken)
    {
        static const bool probe = GetEnvironmentVariableW(L"TS2_VIEWPROBE", nullptr, 0) > 0;
        if (probe && g_frame_no % 120 == 0) {
            const int32_t* v = *img.va<int32_t*>(0x143F0F508);
            uint8_t* cam = *img.va<uint8_t*>(hf2::kCamera);
            if (uint8_t* g = *img.va<uint8_t*>(0x143F359D0)) {
                for (uint32_t off : {0x60u, 0x230u}) {
                    const float* m = reinterpret_cast<const float*>(g + off);
                    log("[matrix +0x%X] %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f\n",
                        off, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13],
                        m[14], m[15]);
                }
                const float* z = reinterpret_cast<const float*>(g + 0x2F0);
                log("[menue3d] +0x2F0: %.3f %.3f %.3f %.3f %.3f\n", z[0], z[1], z[2], z[3], z[4]);
            }
            if (v)
                log("[ausschnitt] %d %d %d %d %d | %d %d %d %d %d | Kamera tan %.4f %.4f fov %.2f\n", v[0], v[1], v[2],
                    v[3], v[4], v[5], v[6], v[7], v[8], v[9], cam ? *reinterpret_cast<float*>(cam + 0x348) : 0.0f,
                    cam ? *reinterpret_cast<float*>(cam + 0x34C) : 0.0f, cam ? *reinterpret_cast<float*>(cam + 0x350) : 0.0f);
        }
    }
    // Rütteln: TS2 setzt die Motoren seines Pads (wie scePadSetActDirect auf
    // der PS2: Pad +0x44 Schüsse, +0x45 Treffer, je 0..127); input.cpp gibt
    // sie an XInput weiter. Das Pad ist der erste int des Spielers.
    {
        int small = 0, large = 0;
        if (const int32_t* pl = *img.va<int32_t*>(hf2::kHudPlayer)) {
            const int32_t pad = pl[0];
            if (pad >= 0 && pad < 8) {
                small = reinterpret_cast<int (*)(int)>(img.va<uint8_t>(hf2::kPadMotor44))(pad);
                large = reinterpret_cast<int (*)(int)>(img.va<uint8_t>(hf2::kPadMotor45))(pad);
            }
        }
        input_rumble(small, large);
    }
    // Freie Kamera (oben bei g_fc_room beschrieben)
    if (g_fc_ready) {
        static bool on = false, was_key = false, was_bit = false;
        static int32_t saved_mode = 0;
        static float pos[3] = {}, yaw = 0, pitch = 0;
        const bool fg = GetForegroundWindow() == HWND(window);
        // Mehrspieler: nur Pads, Tastatur wirkt nicht
        const bool key = fg && !mp_active() && (GetAsyncKeyState(s.freecam_key) & 0x8000) != 0;
        const bool bit = (input_host_bits() & kHostFreecam) != 0;
        const bool toggle = (key && !was_key) || (bit && !was_bit);
        was_key = key;
        was_bit = bit;
        uint8_t* cam = *img.va<uint8_t*>(hf2::kCamera);
        uint8_t* p1 = player1(img);
        auto* mode = img.va<int32_t>(hf2::kCameraMode);
        auto stop = [&](const char* why) {
            if (*mode == 8) *mode = saved_mode;
            on = false;
            g_fc_room = 0;
            input_set_freecam(false);
            log("Freie Kamera aus (%s)\n", why);
        };
        if (toggle && !on && cam && p1) {
            const float* cp = reinterpret_cast<const float*>(cam + hf2::kCamPos);
            const float* cd = reinterpret_cast<const float*>(cam + hf2::kCamDir);
            std::memcpy(pos, cp, sizeof pos);
            yaw = std::atan2(cd[0], cd[2]);
            pitch = std::atan2(cd[1], std::sqrt(cd[0] * cd[0] + cd[2] * cd[2]));
            saved_mode = *mode;
            // was Modus 8 nach [Kamera+0x208]+0x30/+0x34 legt: wie jetzt
            if (uint8_t* proj = *reinterpret_cast<uint8_t**>(cam + hf2::kCamProj))
                std::memcpy(img.va<uint8_t>(hf2::kFreecamExtra), proj + 0x30, 8);
            on = true;
            input_set_freecam(true);
            log("Freie Kamera an bei %.2f %.2f %.2f (Kameramodus %d)\n", pos[0], pos[1], pos[2], saved_mode);
        } else if (toggle && on) {
            stop("Taste");
        } else if (on && (!cam || !p1)) {
            stop("Level zu Ende");
        }
        if (on) {
            const FreecamInput in = input_freecam();
            const float dt = 1.0f / float(s.tick_rate);
            constexpr float kRadPerCount = 0.0025f;
            yaw -= in.look_x * kRadPerCount;
            pitch = std::clamp(pitch - in.look_y * kRadPerCount, -1.5f, 1.5f);
            // rechtshändig wie im Fork: Blick +z, rechts ist -x
            const float c = std::cos(pitch);
            const float f[3] = {std::sin(yaw) * c, std::sin(pitch), std::cos(yaw) * c};
            const float r[3] = {-std::cos(yaw), 0.0f, std::sin(yaw)};
            const float speed = 6.0f * (in.fast ? 4.0f : 1.0f);
            for (int k = 0; k < 3; ++k) pos[k] += (f[k] * in.move_y + r[k] * in.move_x) * speed * dt;
            pos[1] += in.up * speed * dt;
            *mode = 8;
            float* gp = img.va<float>(hf2::kFreecamPos);
            float* gt = img.va<float>(hf2::kFreecamTarget);
            for (int k = 0; k < 3; ++k) gp[k] = pos[k], gt[k] = pos[k] + f[k];
            // Raum an der Kamera-Position; keiner gefunden: den alten behalten
            const auto find_room = reinterpret_cast<FindRoomFn>(img.va<uint8_t>(hf2::kFindRoom));
            const int32_t room = find_room(pos, 1.0f, *reinterpret_cast<float*>(p1 + hf2::kPlayerRoomArg));
            if (room) g_fc_room = room;
        }
    }
    // Nickgrenze (Xbox-Fork: ts2_pitch_limit). Die Globale 0x143F1A390 (Xbox
    // 0x497344, gleich hinter dem aktuellen Spieler) setzt die Spieler-
    // Einrichtung 0x1411D72B0 auf 40; der Blickcode begrenzt den Nickwinkel
    // damit. Solange die Maus zielt, gilt die eigene Grenze, fürs Pad wieder
    // die des Spiels.
    {
        auto* limit = img.va<float>(hf2::kPitchLimit);
        static float game_limit = 0.0f;
        if (*limit != s.pitch_limit && *limit > 0.0f) game_limit = *limit;
        if (input_uses_mouse())
            *limit = s.pitch_limit;
        else if (game_limit > 0.0f)
            *limit = game_limit;
    }
    // Rennen: Taste gehalten (oder umgeschaltet), Shift oder [pad] rennen (LB)
    {
        const bool fg = GetForegroundWindow() == HWND(window);
        const bool run_down = (fg && !mp_active() && (GetAsyncKeyState(s.run_key) & 0x8000) != 0) ||
                              (input_host_bits() & kHostRun);
        static bool was = false, latched = false;
        if (s.run_toggle) {
            if (run_down && !was) latched = !latched;
            g_running = latched;
        } else {
            g_running = run_down;
        }
        // TS2_BLICKPROBE=<takt>: setzt dort den Nickwinkel auf 30° und
        // protokolliert Nickwinkel, Blickzustand (+0x15C) und Nickgrenze
        static const int look_probe = [] {
            wchar_t v[16] = {};
            return GetEnvironmentVariableW(L"TS2_BLICKPROBE", v, 16) > 0 ? _wtoi(v) : 0;
        }();
        static int look_n = -1;  // ab dem ersten Vorwärts-Takt des Skripts
        if (look_probe) {
            if (uint8_t* p1 = player1(img)) {
                if (look_n < 0 && (input_host_bits() & kHostForward)) look_n = 0;
                if (look_n >= 0) ++look_n;
                auto* pitch = reinterpret_cast<float*>(p1 + 0x158);
                if (look_n == look_probe) *pitch = 30.0f;
                if (look_n >= look_probe && look_n < look_probe + 200 && (look_n - look_probe) % 10 == 0)
                    log("[blick] Takt +%d: Nick %.2f, Zustand %d, Grenze %.1f\n", look_n - look_probe, *pitch,
                        *reinterpret_cast<int32_t*>(p1 + 0x15C), *img.va<float>(0x143F1A390));
            }
        }
        // TS2_RENNPROBE=1: Strecke von Spieler 1 je 10 Takte (wie RECOMP_TS2_RUN_TRACE)
        static const bool probe = [] {
            wchar_t v[4] = {};
            return GetEnvironmentVariableW(L"TS2_RENNPROBE", v, 4) > 0 && v[0] == L'1';
        }();
        static float p0[3];
        static int n = 0;
        if (probe) {
            if (uint8_t* p1 = player1(img)) {
                const float* q = reinterpret_cast<const float*>(p1 + hf2::kPlayerPos);
                if (++n % 10 == 0) {
                    const float d = std::sqrt((q[0] - p0[0]) * (q[0] - p0[0]) + (q[2] - p0[2]) * (q[2] - p0[2]));
                    if (d > 0.0f && d < 50.0f) log("[rennen] 10 Takte: %.3f Einheiten, rennt %d\n", d, int(g_running));
                    std::memcpy(p0, q, sizeof p0);
                }
            }
        }
        was = run_down;
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
    const bool down = s.jump && (((GetAsyncKeyState(s.jump_key) & 0x8000) != 0 && GetForegroundWindow() == HWND(window) &&
                                  !mp_active()) ||
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
