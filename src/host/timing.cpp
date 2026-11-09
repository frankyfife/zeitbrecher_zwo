#include "timing.h"

#include <windows.h>

#include <cstring>

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
    if (hz != 120) return;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_period = uint64_t(f.QuadPart) / 120;
    g_time = reinterpret_cast<TimeFn>(hook_call(img, kTimeUpdateJmp, kTimeUpdate, reinterpret_cast<const void*>(&time_hook)));
    if (!g_time) return;
    g_apply = reinterpret_cast<ApplyFn>(
        hook_call(img, kApplyRecordCall, kApplyRecord, reinterpret_cast<const void*>(&apply_record_hook)));
    if (!g_apply) return;
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
