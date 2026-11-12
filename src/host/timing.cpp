#include "timing.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "hooks.h"
#include "log.h"

namespace host {

namespace {

constexpr uint64_t kTickPeriod = 0x142EE39D0;   // uint64, QPC-Einheiten je Takt
constexpr uint64_t kRawTicks = 0x14259CBE8;     // int, Takte dieses Bildes (Begrenzer)
constexpr uint64_t kTicksInt = 0x14259CBD8;     // int
constexpr uint64_t kTicksIntRun = 0x14259CBDC;  // int, Pause 0
constexpr uint64_t kTicksF = 0x14259CBE0;       // float
constexpr uint64_t kTicksFRun = 0x14259CBE4;    // float, Pause 0
constexpr uint64_t kGameTicks = 0x142F9A230;    // uint, Spielzeit in Takten ohne Pause
constexpr uint64_t kTimeUpdate = 0x141222A30;
constexpr uint64_t kTimeUpdateJmp = 0x14107CB02;  // Endaufruf am Bildende (0x14107CADC)
constexpr uint64_t kApplyRecord = 0x14114435C;     // Datensatz eines Spielers -> Bildzeiten
constexpr uint64_t kApplyRecordCall = 0x14114575C; // sein einziger Aufruf (0x1411454EC)

// ganzzahlige Teilungen durch 0x14259CBDC (idiv/div dword ptr [rip+x])
constexpr uint64_t kDivSites[] = {0x1410F9DC9, 0x1410FC280, 0x141114B4F, 0x14112AF75,
                                  0x14115ABBD, 0x1411771AC, 0x1411ECEF7};

const Ts2Image* g_img = nullptr;
bool g_on = false;
uint64_t g_period = 0;
int g_acc = 0;  // halbe 60-Hz-Takte, noch nicht weitergegeben
using TimeFn = void (*)();
TimeFn g_time = nullptr;

template <class T>
T* va(uint64_t a) {
    return g_img->va<T>(a);
}

int g_n120 = 1;         // 120-Hz-Takte des letzten Bildes
bool g_paused = false;

// Pro Spieler und Bild baut 0x1411454EC einen Datensatz aus den Bildzeiten
// (0x1411446A8; Lockstep, für Netzwerkspiel) und überträgt ihn mit
// 0x14114435C zurück: Spielzeit, Pad-Daten und die Takte als Bytes (+0x28,
// +0x29), woraus die Float-Bildzeiten wieder ganze Takte werden. Danach
// gelten wieder die halben Takte; Integer-Takte und Spielzeit bleiben.
using ApplyFn = int (*)(void* record, int player, int flag);
ApplyFn g_apply = nullptr;
int apply_record_hook(void* record, int player, int flag) {
    const int r = g_apply(record, player, flag);
    *va<float>(kTicksF) = float(g_n120) * 0.5f;
    *va<float>(kTicksFRun) = g_paused ? 0.0f : float(g_n120) * 0.5f;
    return r;
}

// Spieler-Aktualisierung (0x14114C990, aus der Spielerschleife 0x1411C6C3C):
// vorher hat die Objektschleife 0x1411FE70C die Float-Bildzeit auf die
// ganzen Takte zurückgesetzt (0 oder 1). Für Spieler und Kamera gelten bei
// 120 Hz halbe Takte in jedem Bild.
using PlayerFn = void (*)();
PlayerFn g_player_update = nullptr;
void player_update_hook() {
    float* t = va<float>(kTicksFRun);
    const float was = *t;
    *t = g_paused ? 0.0f : float(g_n120) * 0.5f;
    g_player_update();
    *t = was;
}

// ---- Stufe 2: Objekte (Gegner, Geschosse, Türen …)
// Die Objektschleife 0x1411FE70C gibt jedem Objekt Takte = Objektuhr −
// Zeitstempel (+0x1C), höchstens 5, als Integer- und Float-Bildzeit, und
// ruft dann 0x1411F3B54(obj, 2, 0) (Aufruf 0x1411FE896), der die
// Aktualisierung an die Klasse des Objekts verteilt. Die Integer-Takte
// bleiben (KI, Timer, Planung "Uhr + N" unverändert); die Float-Bildzeit
// wird die tatsächlich vergangene Zeit in halben Takten, gemessen mit einem
// eigenen 120-Hz-Zeitstempel je Objekt. Passt der nicht zu den Takten des
// Spiels (neues Objekt, wiederverwendete Adresse), gelten die Takte des
// Spiels. Zweimal hintereinander 0 Takte heißt: das Spiel hält das Objekt
// an; dann bleibt die Bildzeit 0.
uint64_t g_now120 = 0;  // 120-Hz-Takte seit dem Start (bei 60 Hz: Bilder)
bool g_half_ticks = false;  // 120 Hz aktiv
bool g_stage2 = false;      // TS2_STUFE2=1: Objekte mit halben Takten (Forschungsstand, Tempo noch falsch)
struct ObjTime {
    uint64_t last = 0;
    int last_d = 0;
    float last_ft = 0;
    float px = 0, py = 0, pz = 0;  // Lage beim letzten Aufruf (+0xCC)
    bool have_pos = false;
    bool tick_moved = false;       // beim letzten Aufruf mit Takt bewegt
    int votes_free = 0, votes_gated = 0;
    bool gated = false;            // bewegt sich nur mit ganzem Takt: Originalwerte
};
std::unordered_map<uint8_t*, ObjTime> g_objects;
using ObjUpdateFn = void (*)(uint8_t* obj, int slot, int64_t arg);
ObjUpdateFn g_obj_update = nullptr;

void object_update_hook(uint8_t* obj, int slot, int64_t arg) {
    float* t = va<float>(kTicksFRun);
    if (!g_paused && *t >= 0.0f) {  // bei 60 Hz (nur Probe) bleibt alles, wie das Spiel es setzt
        const int d = *va<int32_t>(kTicksIntRun);  // Takte dieses Objekts (Spiel)
        ObjTime& o = g_objects[obj];
        // Wie hat sich das Objekt bei seinem letzten Aufruf verhalten? Bewegt
        // es sich in Bildern ohne Takt (mit halbem Takt), läuft es flüssig.
        // Bewegt es sich wiederholt nur in Bildern mit Takt, sperrt sein Code
        // ohne ganzen Takt: dann bekommt es die Originalwerte, sonst liefe es
        // mit halbem Takt nur in jedem zweiten Bild, also zu langsam.
        const float* pos = reinterpret_cast<const float*>(obj + 0xCC);
        if (o.have_pos) {
            const float dx = pos[0] - o.px, dy = pos[1] - o.py, dz = pos[2] - o.pz;
            const bool moved = dx * dx + dy * dy + dz * dz > 1e-10f;
            if (o.last_d == 0 && o.last_ft > 0.0f) {
                if (moved) ++o.votes_free, o.votes_gated = 0;
                else if (o.tick_moved && o.votes_free == 0) ++o.votes_gated;
                if (o.votes_gated >= 3) o.gated = true;
            } else if (o.last_d > 0) {
                o.tick_moved = moved;
            }
        }
        o.px = pos[0], o.py = pos[1], o.pz = pos[2], o.have_pos = true;
        float ft;
        if (o.gated) {
            ft = float(d);  // wie das Spiel: ganze Takte
        } else if (d == 0 && o.last_d == 0 && o.last != 0) {
            ft = 0.0f;  // angehalten
        } else {
            int64_t dt = o.last ? int64_t(g_now120 - o.last) : -1;
            if (dt < 1 || dt > 2 * int64_t(d) + 2) dt = std::max<int64_t>(1, 2 * int64_t(d));
            ft = float(std::min<int64_t>(dt, 10)) * 0.5f;
        }
        if (!g_half_ticks || !g_stage2) ft = *t;
        o.last = g_now120;
        o.last_d = d;
        o.last_ft = ft;
        // TS2_OBJPROBE=1: Lage (+0xCC) und Bildzeit eines bewegten Objekts
        static const bool probe = GetEnvironmentVariableW(L"TS2_OBJPROBE", nullptr, 0) > 0;
        static uint8_t* watched = nullptr;
        static float wx = 0, wz = 0;
        static int logged = 0;
        if (probe && logged < 40) {
            const float* p = reinterpret_cast<const float*>(obj + 0xCC);
            if (!watched && g_now120 > 600) {
                // das erste Objekt, das sich in kleinen Schritten bewegt
                // (fünf Aktualisierungen mit Takt hintereinander)
                struct Seen {
                    float x = 0, z = 0;
                    int moving = 0;
                };
                static std::unordered_map<uint8_t*, Seen> seen;
                Seen& s = seen[obj];
                const float step = std::sqrt((p[0] - s.x) * (p[0] - s.x) + (p[2] - s.z) * (p[2] - s.z));
                if (d > 0) s.moving = (step > 0.001f && step < 2.0f) ? s.moving + 1 : 0;
                if (s.moving >= 5) watched = obj, wx = p[0], wz = p[2];
                s.x = p[0], s.z = p[2];
            }
            if (obj == watched) {
                log("[objekt] %p Bild %llu: Takte %d, Bildzeit Spiel %.2f -> %.2f, Lage %.3f %.3f (Schritt %.4f)\n", obj,
                    static_cast<unsigned long long>(g_now120), d, *t, ft, p[0], p[2],
                    std::sqrt((p[0] - wx) * (p[0] - wx) + (p[2] - wz) * (p[2] - wz)));
                wx = p[0], wz = p[2];
                ++logged;
            }
        }
        *t = ft;
    }
    // TS2_OBJSUMME=1: Strecke aller Objekte in Sekunde 34–44 nach dem Start
    static const bool sum = GetEnvironmentVariableW(L"TS2_OBJSUMME", nullptr, 0) > 0;
    if (!sum) {
        g_obj_update(obj, slot, arg);
        return;
    }
    // Strecke seit dem letzten Aufruf desselben Objekts (bewegt wird in den
    // Rückrufen nach 0x1411F3B54)
    struct Last {
        float x, y, z;
    };
    static std::unordered_map<uint8_t*, Last> last;
    const float* p = reinterpret_cast<const float*>(obj + 0xCC);
    static double total = 0;
    static long moves = 0;
    static bool done = false;
    const double secs = double(g_now120) / (g_half_ticks ? 120.0 : 60.0);
    auto it = last.find(obj);
    if (secs >= 34.0 && secs < 44.0 && it != last.end()) {
        const Last& l = it->second;
        const double d = std::sqrt(double(p[0] - l.x) * (p[0] - l.x) + double(p[1] - l.y) * (p[1] - l.y) +
                                   double(p[2] - l.z) * (p[2] - l.z));
        if (d > 0.0 && d < 50.0) total += d, ++moves;
    } else if (secs >= 44.0 && !done) {
        done = true;
        int gated = 0, free_ = 0;
        for (auto& [k, v] : g_objects) gated += v.gated, free_ += v.votes_free > 0;
        log("[objekte] Sekunde 34-44: Strecke aller Objekte %.1f in %ld Schritten; %d Objekte flüssig, %d mit ganzen Takten\n",
            total, moves, free_, gated);
    }
    last[obj] = {p[0], p[1], p[2]};
    g_obj_update(obj, slot, arg);
}

// ---- Takt vor der Eingabe (beide Takte, 60 und 120 Hz)
// TS2s Begrenzer 0x14107CC64 läuft am Ende von Render: Zeitfortschreibung,
// dann warten bis zur nächsten Periode, Takte zählen. Damit lag zwischen dem
// Einlesen der Maus (vor Tick) und Present fast eine ganze Periode Warten
// (gemessen 16,5 ms bei 60 Hz, 8,2 ms bei 120 Hz). Jetzt wartet ts2.exe am
// Anfang des Bildes (timing_wait) bis zur nächsten Periode und liest danach
// die Eingabe. Der Begrenzer bekommt für seine Uhr (QPC-Aufrufe 0x14107CC8C
// am Anfang, 0x14107CDE8 am Ende) die Periodengrenze, bis zu der ts2.exe
// gewartet hat: Er sieht dann ganze Perioden (Takte wie bisher, Rest in
// seinem Übertrag 0x142EE3A00) und wartet selbst nicht mehr.
constexpr uint64_t kQpcAtStart = 0x14107CC8C, kQpcAtEnd = 0x14107CDE8, kQpcFn = 0x141069BD0;
constexpr uint64_t kLimLast = 0x142EE39D8;    // uint64: Uhr am Ende des letzten Begrenzers
constexpr uint64_t kLimTarget = 0x142EE39F8;  // uint64: Mindestabstand (Periode beim letzten Mal)
using QpcFn = uint64_t (*)();
QpcFn g_qpc = nullptr;
bool g_pace = false;        // Umwege gesetzt
bool g_lim_ran = false;     // Begrenzer lief im letzten Bild (nur dann wartet ts2.exe)
bool g_v_valid = false;     // dieses Bild hat ts2.exe gewartet: g_v gilt
uint64_t g_v = 0;           // Uhr für den Begrenzer in diesem Bild
uint64_t g_grid = 0;        // zuletzt an den Begrenzer gegebene Uhr
uint64_t g_real_prev = 0;   // Monitor-Kopplung: echte Zeit beim letzten Aufwachen
uint64_t g_qpf = 0;
int g_display_interval = 0; // Monitor-Kopplung: Bildwechsel je Bild (0 = aus)

uint64_t qpc_now() {
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return uint64_t(q.QuadPart);
}

// genau bis `until` (QPC) warten: hochauflösender Zeitgeber bis ~1 ms davor,
// den Rest aktiv
void wait_until(uint64_t until) {
    static HANDLE timer =
        CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    for (;;) {
        const uint64_t now = qpc_now();
        if (now >= until) return;
        const uint64_t left = until - now;
        if (timer && left > g_qpf / 600) {  // > 1,67 ms
            LARGE_INTEGER due;
            due.QuadPart = -LONGLONG((left - g_qpf / 1000) * 10000000 / g_qpf);  // 100 ns, relativ, 1 ms früher
            SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(timer, INFINITE);
        } else {
            YieldProcessor();
        }
    }
}

uint64_t qpc_at_start_hook() {
    g_lim_ran = true;
    if (!g_v_valid) return g_qpc();  // wie bisher: der Begrenzer wartet selbst
    // nie unter dem Mindestabstand: sonst wartete der Begrenzer (mit dieser
    // Uhr für immer)
    const uint64_t floor_v = *va<uint64_t>(kLimLast) + *va<uint64_t>(kLimTarget);
    if (g_v < floor_v) g_v = floor_v;
    return g_v;
}

uint64_t qpc_at_end_hook() {
    const uint64_t v = g_v_valid ? g_v : g_qpc();
    g_grid = v;
    g_v_valid = false;
    return v;
}

// bei 60 Hz mit Objektprobe: nur die Bilder zählen
void time_probe_hook() {
    g_time();
    ++g_now120;
}

void time_hook() {
    int32_t* raw = va<int32_t>(kRawTicks);
    const int n120 = *raw;
    g_acc += n120;
    const int n60 = g_acc / 2;
    g_acc -= n60 * 2;
    uint32_t* game = va<uint32_t>(kGameTicks);
    const uint32_t game_before = *game;
    *raw = n60;  // Spielzeit (0x142F9A22C) in 60-Hz-Takten
    g_time();
    *raw = n120;
    const bool paused = *va<float>(kTicksFRun) == 0.0f && *va<int32_t>(kTicksIntRun) == 0;
    g_n120 = n120;
    g_now120 += uint64_t(n120);
    g_paused = paused;
    *va<int32_t>(kTicksInt) = n60;
    *va<int32_t>(kTicksIntRun) = paused ? 0 : n60;
    *va<float>(kTicksF) = float(n120) * 0.5f;
    *va<float>(kTicksFRun) = paused ? 0.0f : float(n120) * 0.5f;
    *game = game_before + (paused ? 0u : uint32_t(n60));
}

}  // namespace

void timing_install(const Ts2Image& img, int hz) {
    g_img = &img;
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        g_qpf = uint64_t(f.QuadPart);
        // TS2_TAKTALT=1: wie bisher (Begrenzer wartet nach dem Zeichnen; Vergleiche)
        if (GetEnvironmentVariableW(L"TS2_TAKTALT", nullptr, 0) == 0) {
            g_qpc = reinterpret_cast<QpcFn>(
                hook_call(img, kQpcAtStart, kQpcFn, reinterpret_cast<const void*>(&qpc_at_start_hook)));
            if (g_qpc && hook_call(img, kQpcAtEnd, kQpcFn, reinterpret_cast<const void*>(&qpc_at_end_hook)))
                g_pace = true;
            log("Takt: %s\n", g_pace ? "Warten vor der Eingabe (kurze Latenz)" : "Umweg fehlgeschlagen, Begrenzer wie bisher");
        }
    }
    if (hz != 120) {
        // TS2_OBJPROBE bei 60 Hz: nur beobachten (Vergleichswerte)
        if (GetEnvironmentVariableW(L"TS2_OBJPROBE", nullptr, 0) > 0) {
            g_obj_update = reinterpret_cast<ObjUpdateFn>(
                hook_call(img, 0x1411FE896, 0x1411F3B54, reinterpret_cast<const void*>(&object_update_hook)));
            g_time = reinterpret_cast<TimeFn>(
                hook_call(img, kTimeUpdateJmp, kTimeUpdate, reinterpret_cast<const void*>(&time_probe_hook)));
        }
        return;
    }
    g_half_ticks = true;
    {
        wchar_t v[4] = {};
        if (GetEnvironmentVariableW(L"TS2_STUFE2", v, 4) > 0 && v[0] == L'1') g_stage2 = true;
    }
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_period = uint64_t(f.QuadPart) / 120;
    g_time = reinterpret_cast<TimeFn>(hook_call(img, kTimeUpdateJmp, kTimeUpdate, reinterpret_cast<const void*>(&time_hook)));
    if (!g_time) return;
    g_apply = reinterpret_cast<ApplyFn>(
        hook_call(img, kApplyRecordCall, kApplyRecord, reinterpret_cast<const void*>(&apply_record_hook)));
    if (!g_apply) return;
    // Objekt-Umweg nur für Stufe 2 oder die Objektproben
    if (g_stage2 || GetEnvironmentVariableW(L"TS2_OBJSUMME", nullptr, 0) > 0 ||
        GetEnvironmentVariableW(L"TS2_OBJPROBE", nullptr, 0) > 0)
        g_obj_update = reinterpret_cast<ObjUpdateFn>(
            hook_call(img, 0x1411FE896, 0x1411F3B54, reinterpret_cast<const void*>(&object_update_hook)));
    for (uint64_t site : {0x1411C6C93ull, 0x1411C6D99ull}) {
        auto* f = reinterpret_cast<PlayerFn>(
            hook_call(img, site, 0x14114C990, reinterpret_cast<const void*>(&player_update_hook)));
        if (f) g_player_update = f;
    }
    // Teilungen: bei 0 Takten Quotient 0x7FFFFFFF ("kein Ereignis in diesem Bild")
    const uint64_t divisor = reinterpret_cast<uint64_t>(img.va(kTicksIntRun));
    int patched = 0;
    for (uint64_t site : kDivSites) {
        uint8_t* p = img.va(site);
        int32_t rel;
        std::memcpy(&rel, p + 2, 4);
        const bool is_idiv = p[0] == 0xF7 && p[1] == 0x3D, is_div = p[0] == 0xF7 && p[1] == 0x35;
        if ((!is_idiv && !is_div) || p + 6 + rel != img.va(kTicksIntRun)) {
            log("[takt] Teilung bei 0x%llX nicht wie erwartet\n", static_cast<unsigned long long>(site));
            continue;
        }
        uint8_t expected[6];
        std::memcpy(expected, p, 6);
        uint8_t stub[] = {
            0x51,                                            // push rcx
            0x48, 0xB9, 0, 0, 0, 0, 0, 0, 0, 0,              // mov rcx, &Takte
            0x8B, 0x09,                                      // mov ecx, [rcx]
            0x85, 0xC9,                                      // test ecx, ecx
            0x74, 0x04,                                      // jz null
            0xF7, uint8_t(is_idiv ? 0xF9 : 0xF1),            // idiv/div ecx
            0x59,                                            // pop rcx
            0xC3,                                            // ret
            0xB8, 0xFF, 0xFF, 0xFF, 0x7F,                    // null: mov eax, 0x7FFFFFFF
            0x59,                                            // pop rcx
            0xC3,                                            // ret
        };
        std::memcpy(stub + 3, &divisor, 8);
        patched += patch_with_call(img, site, expected, 6, stub, sizeof stub);
    }
    g_on = true;
    log("Bildtakt 120 Hz (Spieler und Kamera nativ, Welt in 60-Hz-Takten): Periode %llu QPC-Einheiten, %d/7 Teilungen abgesichert\n",
        static_cast<unsigned long long>(g_period), patched);
}

void timing_set_display_interval(int n) { g_display_interval = n; }

void timing_wait() {
    if (!g_pace) return;
    const bool ran = g_lim_ran;
    g_lim_ran = false;
    g_v_valid = false;
    // Begrenzer lief im letzten Bild nicht (Laden, Start): kein Warten, er
    // nimmt beim nächsten Mal wieder die echte Uhr
    const uint64_t P = *va<uint64_t>(kTickPeriod);
    if (!ran || !g_grid || !P) {
        g_real_prev = 0;
        return;
    }
    if (g_display_interval > 0) {
        // Monitor-Kopplung: main.cpp hat schon auf den Bildwechsel gewartet
        // (Waitable Object). Takte = vergangene Zeit in Perioden, gerundet:
        // bei Bildwechsel = Takt genau einer je Bild, ohne Schwebung.
        uint64_t now = qpc_now();
        if (g_real_prev && now - g_real_prev < P / 2) {
            // zu früh (Monitor schneller als gedacht): nach der Uhr warten
            wait_until(g_real_prev + P);
            now = qpc_now();
        }
        int64_t n = g_real_prev ? (int64_t(now - g_real_prev) + int64_t(P / 2)) / int64_t(P) : 1;
        n = std::clamp<int64_t>(n, 1, 4);
        g_real_prev = now;
        g_v = g_grid + uint64_t(n) * P;
    } else {
        const uint64_t target = g_grid + P;
        const uint64_t now = qpc_now();
        if (now < target) {
            wait_until(target);
            g_v = target;
        } else {
            const uint64_t n = (now - g_grid) / P;
            g_v = n > 4 ? now : g_grid + n * P;  // weit zurück (Ladepause): neu aufsetzen
        }
    }
    g_v_valid = true;
}

void timing_frame() {
    // TS2_TAKTPROBE=1: Spielzeit in Takten je Sekunde Wanduhr (soll 60 sein)
    static const bool probe = [] {
        wchar_t v[4] = {};
        return GetEnvironmentVariableW(L"TS2_TAKTPROBE", v, 4) > 0;
    }();
    if (probe && g_img) {
        static LARGE_INTEGER t0{}, f{};
        static uint32_t g0 = 0, all0 = 0;
        static int n = 0;
        if (!f.QuadPart) QueryPerformanceFrequency(&f), QueryPerformanceCounter(&t0);
        if (++n % 600 == 0) {
            LARGE_INTEGER t;
            QueryPerformanceCounter(&t);
            const double s = double(t.QuadPart - t0.QuadPart) / double(f.QuadPart);
            const uint32_t g = *va<uint32_t>(kGameTicks), all = *va<uint32_t>(0x142F9A22C);
            log("[takt] %.2f s: Spielzeit +%u Takte (%.1f/s), alle +%u (%.1f/s), Bildzeit %.2f\n", s, g - g0,
                (g - g0) / s, all - all0, (all - all0) / s, *va<float>(kTicksFRun));
            g0 = g, all0 = all, t0 = t;
        }
    }
    if (!g_on) return;
    // TS2 setzt die Periode bei Start und Levelwechsel auf 1/60 s (0x14107CB08)
    *va<uint64_t>(kTickPeriod) = g_period;
}

}  // namespace host
