#include "spritefix.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include "hooks.h"
#include "log.h"

namespace host {

namespace {

const Ts2Image* g_img = nullptr;

// mulss xmm0, [rip+1/1080] in den beiden Schreibern von Kamera+0x27C
constexpr uint64_t kFactorSites[] = {0x141083653, 0x1410BC49F};
// movss xmm0, [rip+250] (Obergrenze in 0x1410F4494)
constexpr uint64_t kCap250Site = 0x1410F45AC;
// Aufrufe der Listenzeichnung 0x14112F3C0 in 0x14112FA10
constexpr uint64_t kListCalls[] = {0x14112FA4F, 0x14112FAA3};
constexpr uint64_t kDrawList = 0x14112F3C0;
constexpr uint64_t kScreen = 0x143F0F508;  // Zeiger auf die Sicht, +0x18 Höhe

using DrawListFn = void(__fastcall*)(void*, int, uint8_t*, int);
DrawListFn g_draw_list = nullptr;
bool g_probe = false;  // TS2_SPRITEPROBE

float screen_scale() {
    const int32_t* vp = *g_img->va<int32_t*>(kScreen);
    const int h = vp ? vp[6] : 0;
    return h > 0 ? float(h) / 480.0f : 1.0f;
}

// Teilchen einer Liste zeichnen; Obergrenze je Teilchen (+0x40) für die
// Dauer in Bildpunkten der echten Bildhöhe
void __fastcall draw_list(void* ctx, int list, uint8_t* head, int mask) {
    const float k = screen_scale();
    std::vector<std::pair<float*, float>> caps;
    for (uint8_t* p = head; p && caps.size() < 4096; p = *reinterpret_cast<uint8_t**>(p + 0x68)) {
        float* cap = reinterpret_cast<float*>(p + 0x40);
        caps.emplace_back(cap, *cap);
        *cap *= k;
    }
    g_draw_list(ctx, list, head, mask);
    for (auto& [cap, v] : caps) *cap = v;
    if (g_probe) {
        // je 300 Takte: Teilchen in den Listen (Summe über die Aufrufe)
        static uint64_t walked = 0;
        static int last = -1;
        walked += caps.size();
        const int tick = *g_img->va<int32_t>(0x142EE3DE8);
        if (tick / 300 != last) {
            if (last >= 0) log("[sprite] Takte %d–%d: %llu Teilchen in Listen\n", last * 300, last * 300 + 299,
                               static_cast<unsigned long long>(walked));
            last = tick / 300;
            walked = 0;
        }
    }
}

// Befehl "op xmm0, [rip+x]" (8 Byte) durch einen Aufruf von
// "op xmm0, [rip+1]; ret; <wert>" ersetzen
bool patch_const(uint64_t va, float value) {
    uint8_t expected[8];
    std::memcpy(expected, g_img->va<uint8_t>(va), 8);
    if (expected[0] != 0xF3 || expected[1] != 0x0F || expected[3] != 0x05) return false;
    uint8_t stub[13] = {0xF3, 0x0F, expected[2], 0x05, 1, 0, 0, 0, 0xC3};
    std::memcpy(stub + 9, &value, 4);
    return patch_with_call(*g_img, va, expected, 8, stub, sizeof stub);
}

// ---- Probe TS2_SPRITEPROBE=1: zählt, wie oft jede Größenrechnung mit
// Kamera+0x27C läuft (Protokoll beim Beenden)
struct Reader {
    uint64_t va;
    size_t len;
    const char* what;
};
constexpr Reader kReaders[] = {
    {0x1410EDD82, 8, "Effekt 0x1410EDC30"},  {0x1410EE832, 8, "Effekt 0x1410EE690"},
    {0x1410F1AF9, 8, "Effekt 0x1410F1A04"},  {0x1410F45C9, 8, "Effekt 0x1410F4494 (bis 250)"},
    {0x1410F4C18, 8, "Effekt 0x1410F4750"},  {0x141115390, 8, "Schein glow_64 0x141115044"},
    {0x1411160A2, 8, "muzzle 0x141115F40"},  {0x141125B12, 8, "muzzle 0x141125950"},
    {0x14112F6E6, 9, "Teilchen 0x14112F3C0"}, {0x1411D829A, 8, "0x1411D81B4"},
};
constexpr size_t kReaderCount = sizeof kReaders / sizeof kReaders[0];
uint32_t g_counts[kReaderCount] = {};

void report_counts() {
    for (size_t i = 0; i < kReaderCount; ++i) log("[sprite] %-32s %u\n", kReaders[i].what, g_counts[i]);
}

void install_probe() {
    for (size_t i = 0; i < kReaderCount; ++i) {
        const Reader& r = kReaders[i];
        // pushfq; push rax; mov rax, &Zähler; inc dword [rax]; pop rax; popfq; <Befehl>; ret
        uint8_t stub[32] = {0x9C, 0x50, 0x48, 0xB8};
        const uint64_t at = reinterpret_cast<uint64_t>(&g_counts[i]);
        std::memcpy(stub + 4, &at, 8);
        const uint8_t tail[] = {0xFF, 0x00, 0x58, 0x9D};
        std::memcpy(stub + 12, tail, 4);
        std::memcpy(stub + 16, g_img->va<uint8_t>(r.va), r.len);
        stub[16 + r.len] = 0xC3;
        std::vector<uint8_t> expected(g_img->va<uint8_t>(r.va), g_img->va<uint8_t>(r.va) + r.len);
        patch_with_call(*g_img, r.va, expected.data(), r.len, stub, 17 + r.len);
    }
    g_probe = true;
    atexit(report_counts);
    log("[sprite] Probe an: %zu Größenrechnungen gezählt\n", kReaderCount);
}

}  // namespace

bool spritefix_install(const Ts2Image& img, int height) {
    g_img = &img;
    wchar_t v[8] = {};
    if (GetEnvironmentVariableW(L"TS2_SPRITEPROBE", v, 8) > 0) install_probe();
    if (GetEnvironmentVariableW(L"TS2_TEILCHENFIX", v, 8) > 0 && v[0] == L'0') {
        log("Teilchen und Sprites: Größe wie im PC-Port (TS2_TEILCHENFIX=0)\n");
        return false;
    }
    int n = 0;
    for (uint64_t va : kFactorSites)
        if (*img.va<uint8_t>(va + 2) == 0x59 && patch_const(va, 1.0f / 480.0f)) ++n;
    const bool cap = *img.va<uint8_t>(kCap250Site + 2) == 0x10 &&
                     patch_const(kCap250Site, 250.0f * float(height > 0 ? height : 1080) / 480.0f);
    int lists = 0;
    for (uint64_t va : kListCalls) {
        void* orig = hook_call(img, va, kDrawList, reinterpret_cast<const void*>(&draw_list));
        if (orig) g_draw_list = reinterpret_cast<DrawListFn>(orig), ++lists;
    }
    log("Teilchen und Sprites: Größe in Bildhöhe/480 wie Xbox (%d/2 Faktoren, Obergrenzen %d/2 Listen, 250er %s)\n",
        n, lists, cap ? "ja" : "nein");
    return n == 2;
}

}  // namespace host
