#include "hudfix.h"

#include <windows.h>
#include <unknwn.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "hf2.h"
#include "hooks.h"
#include "log.h"

namespace host {

namespace {

const Ts2Image* g_img = nullptr;

// ---- Figurenwahl: Absturz nach vielen Figuren (TS2-Redux-Issue #5)
//
// TS2 verwaltet Texturen über Beschreiber (Bank *0x14259D5E0, 1024 je Level,
// je 0x38 Byte; +0x18 Flags, +0x1C Texturnummer, +0x34 D3D-Nummer). Der
// Port hat dazu eine D3D-Tabelle (0x142FE59A0, 4096 Plätze: Textur, SRV,
// Sampler) mit einem Zähler, der nur wächst (0x141229FD0), und hängt jede
// neue D3D-Nummer an eine Liste (0x14122B41C) mit 1024 Plätzen, die niemand
// liest. Die Figurenwahl lädt bei jedem Wechsel ein Modell; das alte gibt
// seine Beschreiber frei (0x14107387C → 0x14122C918 setzt Flag 0x800000),
// sie werden wiederverwendet, sobald die Bank leer ist (0x14122C33C). Die
// D3D-Textur des alten Inhalts gab der Port aber nie frei: jede Figur
// kostete 8–10 neue D3D-Plätze und Listeneinträge. Nach rund 90 Figuren
// lief die Liste über; dahinter liegen Zeichenzustände und die
// Upload-Warteschlange (0x142FFEB90, je 0x20 Byte: Ziel, SRV, Daten,
// Zeilenlänge), deren erstes Ziel so überschrieben wurde, und
// UpdateSubresource (0x14122A21F) griff ins Leere. Später wäre auch die
// D3D-Tabelle übergelaufen.
//
// Abhilfe: (1) Wird ein freigegebener Beschreiber wiederverwendet, gibt
// ts2.exe dessen alte D3D-Textur frei und merkt sich den Platz; (2) das
// Anlegen einer D3D-Textur nimmt zuerst einen solchen Platz; (3) die Liste
// bekommt eine Grenze.
struct TexDesc {  // 0x38 Byte
    uint8_t pad0[0x18];
    uint32_t flags;  // 0x800000 frei
    int32_t number;
    uint8_t pad20[0x14];
    int32_t d3d;     // +0x34
};
static_assert(sizeof(TexDesc) == 0x38, "Beschreiber");
struct TexBank {
    TexDesc* desc;
    uint8_t* aux;
    int32_t next;   // nächster unbenutzter, absteigend; < 0: Bank leer
    int32_t count;
};
struct D3DSlot {
    IUnknown* tex;
    IUnknown* srv;
    void* sampler;  // geteilt, nicht freigeben
};
struct Upload {  // Warteschlange 0x142FFEB90
    IUnknown* dst;
    void* srv;
    void* data;
    uint32_t pitch, mips;
};

D3DSlot* g_slots = nullptr;
int32_t* g_slot_count = nullptr;   // 0x143020A80
int32_t* g_slot_system = nullptr;  // 0x143020A84
int32_t g_free_slots[4096];
int g_free_n = 0;
uint64_t g_released = 0, g_reused = 0;

bool slot_in_rt_list(int32_t idx) {  // Render-Ziele (0x14122B334): eigene Freigabe 0x14122B184
    const int32_t n = *g_img->va<int32_t>(hf2::kRtListCount);
    const int32_t* l = g_img->va<int32_t>(hf2::kRtList);
    for (int32_t i = 0; i < n && i < 1024; ++i)
        if (l[i] == idx) return true;
    return false;
}

bool slot_uploading(const IUnknown* tex) {
    const int32_t n = *g_img->va<int32_t>(hf2::kUploadCount);
    const Upload* q = g_img->va<Upload>(hf2::kUploadQueue);
    for (int32_t i = 0; i < n && i < 4096; ++i)
        if (q[i].dst == tex) return true;
    return false;
}

void release_slot_of(const TexDesc& d) {
    const int32_t idx = d.d3d;
    if (idx < *g_slot_system || idx >= *g_slot_count || g_free_n >= 4096) return;
    D3DSlot& s = g_slots[idx];
    if (!s.tex || slot_in_rt_list(idx) || slot_uploading(s.tex)) return;
    s.tex->Release();
    if (s.srv) s.srv->Release();
    s = D3DSlot{};
    g_free_slots[g_free_n++] = idx;
    ++g_released;
}

// Ersatz für 0x14122C33C (über tex_desc_alloc_thunk, das r10/r11 erhält):
// wie das Original, nur mit Freigabe der alten D3D-Textur
int32_t tex_desc_alloc() {
    TexBank* bank = *g_img->va<TexBank*>(hf2::kTexBank);
    if (bank->next >= 0) return bank->next--;
    for (int32_t i = 0; i < bank->count; ++i) {
        TexDesc& d = bank->desc[i];
        if (d.flags & 0x800000u) {
            d.flags &= ~0x800000u;
            release_slot_of(d);
            return i;
        }
    }
    return 0;
}

// Aufrufe von 0x141229FD0 (D3D-Textur anlegen, liefert die Nummer)
using CreateFn = int32_t (*)(int32_t, int32_t, int32_t, int32_t, void*, int32_t);
CreateFn g_create = nullptr;
int32_t create_hook(int32_t w, int32_t h, int32_t a, int32_t b, void* data, int32_t c) {
    while (g_free_n > 0) {
        const int32_t idx = g_free_slots[--g_free_n];
        // nach einem Levelwechsel (0x14122B2B4) sind alte Einträge ungültig
        if (idx < *g_slot_system || idx >= *g_slot_count || g_slots[idx].tex) continue;
        const int32_t saved = *g_slot_count;
        *g_slot_count = idx;
        const int32_t r = g_create(w, h, a, b, data, c);
        *g_slot_count = saved;
        ++g_reused;
        return r;
    }
    return g_create(w, h, a, b, data, c);
}

int32_t* g_list = nullptr;
int32_t* g_list_n = nullptr;
bool g_list_full_logged = false;

void tex_list_add(int32_t index) {
    const int32_t n = *g_list_n;
    if (n < int32_t(hf2::kTexListCap)) {
        g_list[n] = index;
        *g_list_n = n + 1;
    } else if (!g_list_full_logged) {
        g_list_full_logged = true;
        log("[hud] Texturliste voll (%u), weitere Einträge entfallen (früher: Absturz)\n", hf2::kTexListCap);
    }
}

bool install_tex_list_guard(const Ts2Image& img) {
    // 0x14122B41C: movsxd rdx,[rip+..]; lea r8,[rip+..]; mov [r8+rdx*4],ecx;
    // inc edx; mov [rip+..],edx; ret (27 Byte)
    static const uint8_t kOrig[] = {0x48, 0x63, 0x15, 0x6D, 0x56, 0xDF, 0x01, 0x4C, 0x8D, 0x05, 0x36, 0x27, 0xDD, 0x01,
                                    0x41, 0x89, 0x0C, 0x90, 0xFF, 0xC2, 0x89, 0x15, 0x5A, 0x56, 0xDF, 0x01, 0xC3};
    uint8_t* at = img.va(hf2::kTexListAdd);
    if (std::memcmp(at, kOrig, sizeof kOrig) != 0) return false;
    g_list = img.va<int32_t>(hf2::kTexList);
    g_list_n = img.va<int32_t>(hf2::kTexListCount);
    uint8_t jmp[12] = {0x48, 0xB8};  // mov rax, imm64; jmp rax
    const uint64_t target = reinterpret_cast<uint64_t>(&tex_list_add);
    std::memcpy(jmp + 2, &target, 8);
    jmp[10] = 0xFF;
    jmp[11] = 0xE0;
    DWORD old;
    VirtualProtect(at, sizeof jmp, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(at, jmp, sizeof jmp);
    VirtualProtect(at, sizeof jmp, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, sizeof jmp);
    return true;
}

bool install_tex_recycling(const Ts2Image& img) {
    static const uint8_t kOrigAlloc[] = {0x48, 0x8B, 0x15, 0x9D, 0x12, 0x37, 0x01, 0x33, 0xC0, 0x39, 0x42, 0x10, 0x7C};
    uint8_t* at = img.va(hf2::kTexDescAlloc);
    if (std::memcmp(at, kOrigAlloc, sizeof kOrigAlloc) != 0) return false;
    g_slots = img.va<D3DSlot>(hf2::kD3DSlots);
    g_slot_count = img.va<int32_t>(hf2::kD3DTexCount);
    g_slot_system = img.va<int32_t>(hf2::kD3DTexSystem);
    for (uint64_t call : hf2::kD3DCreateCalls) {
        void* prev = hook_call(img, call, hf2::kD3DCreate, reinterpret_cast<const void*>(&create_hook));
        if (!prev) return false;  // (die erste Stelle bliebe dann umgelenkt; g_create ist gesetzt)
        g_create = reinterpret_cast<CreateFn>(prev);
    }
    // 0x14122C33C: Aufrufer (0x14122C49C, 0x14122C4EC) halten r10/r11 über den
    // Aufruf; das Original fasst nur rax, rcx, rdx, r8, r9 an.
    uint8_t thunk[29] = {0x41, 0x52, 0x41, 0x53, 0x48, 0x83, 0xEC, 0x28, 0x48, 0xB8};  // push r10/r11; sub rsp,28h; mov rax,
    const uint64_t target = reinterpret_cast<uint64_t>(&tex_desc_alloc);
    std::memcpy(thunk + 10, &target, 8);
    const uint8_t tail[] = {0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x28, 0x41, 0x5B, 0x41, 0x5A, 0xC3};  // call rax; ...; ret
    std::memcpy(thunk + 18, tail, sizeof tail);
    DWORD old;
    VirtualProtect(at, sizeof thunk, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(at, thunk, sizeof thunk);
    VirtualProtect(at, sizeof thunk, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, sizeof thunk);
    return true;
}

// ---- Probe (TS2_HUDPROBE=1)
// Abstürze: Modul, Abwicklung bis in den TS2-Code, mögliche
// TS2-Rücksprungadressen auf dem Stapel. Figurenwahl: Texturzähler je Figur.
LONG CALLBACK probe_exception(EXCEPTION_POINTERS* e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    const uint64_t lo = uint64_t(g_img->base) + (hf2::kTs2Lo - g_img->preferred_base);
    const uint64_t hi = uint64_t(g_img->base) + (hf2::kTs2Hi - g_img->preferred_base);
    auto orig = [](uint64_t a) { return (unsigned long long)(a - uint64_t(g_img->base) + g_img->preferred_base); };
    std::string chain;
    char buf[48];
    const auto* sp = reinterpret_cast<const uint64_t*>(e->ContextRecord->Rsp);
    for (int i = 0; i < 256; ++i)
        if (sp[i] >= lo && sp[i] < hi) {
            std::snprintf(buf, sizeof buf, " [%d]%llX", i, orig(sp[i]));
            chain += buf;
        }
    log("[hudprobe] Absturz, TS2-Adressen auf dem Stapel:%s\n", chain.c_str());
    CONTEXT c = *e->ContextRecord;
    for (int i = 0; i < 16; ++i) {
        DWORD64 base = 0;
        RUNTIME_FUNCTION* f = RtlLookupFunctionEntry(c.Rip, &base, nullptr);
        char mn[MAX_PATH] = "?";
        HMODULE mm = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(c.Rip), &mm))
            GetModuleFileNameA(mm, mn, MAX_PATH);
        const bool ts2 = c.Rip >= uint64_t(g_img->base) && c.Rip < uint64_t(g_img->base) + g_img->size;
        log("[hudprobe]  #%d %s+0x%llX rcx=%llX rdx=%llX\n", i, ts2 ? "TS2 VA" : mn,
            ts2 ? orig(c.Rip) : (unsigned long long)(c.Rip - uint64_t(mm)), (unsigned long long)c.Rcx,
            (unsigned long long)c.Rdx);
        if (!f) break;
        void* hd = nullptr;
        DWORD64 est = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, f, &c, &hd, &est, nullptr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

using ModelFn = void (*)(int, int);
ModelFn g_model_load = nullptr;
void model_load_probe(int player, int chr) {
    struct Bank { uint8_t* desc; uint8_t* aux; int32_t next; int32_t count; };
    const Bank* bank = *g_img->va<Bank*>(hf2::kTexBank);
    log("[hudprobe] Figur %d/%d: D3D-Texturen %d, Liste %d, Beschreiber frei ab %d von %d, D3D frei %llu/wieder %llu\n",
        player, chr, *g_img->va<int32_t>(hf2::kD3DTexCount), *g_img->va<int32_t>(hf2::kTexListCount), bank->next,
        bank->count, (unsigned long long)g_released, (unsigned long long)g_reused);
    g_model_load(player, chr);
}

// ---- Gegnerbalken der Endgegner (TS2-Redux-Issue #20)
//
// 0x141140590 zeichnet 20 Segmente (0x141088D98, Viereck mit vier
// Eckenfarben 0xAARRGGBB). Volle Segmente bekommen Tabellenfarbe × 1,4,
// leere × 1,4 × 0,7 (Übergang beim Abnehmen), Alpha 0x7F bzw. 0x6E. Die
// Werte sind für die Konsolen-Regel "0x80 = voll" gedacht: Auf der Konsole
// ist volles Gelb 177/255, leeres 124/255, deutlich dunkler. Der
// Vertex-Shader des PC-Ports (ts.vp) multipliziert jede Eckenfarbe mit
// 255/127; Gelb und Rot sättigen so voll wie leer bei 255, nur das Alpha
// unterscheidet sich (100 % gegen 86 %), und der Balken sieht immer voll
// aus. ts2.exe halbiert für diese Vierecke die Farbanteile (das Alpha
// bleibt, dort stimmt die Verdopplung mit der Konsole überein).
using QuadFn = void (*)(float, float, float, float, uint32_t, uint32_t, uint32_t, uint32_t);
QuadFn g_quad = nullptr;
uint32_t half_rgb(uint32_t c) { return (c & 0xFF000000u) | ((c >> 1) & 0x007F7F7Fu); }
void enemy_bar_quad(float x0, float y0, float x1, float y1, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3) {
    g_quad(x0, y0, x1, y1, half_rgb(c0), half_rgb(c1), half_rgb(c2), half_rgb(c3));
}

// Test (TS2_BOSSTEST=<n>, n volle Segmente von links, -n von rechts): nach dem HUD zeichnet ts2.exe
// den Gegnerbalken (0x141140590) eines Endgegners an der Stelle, an der TS2
// ihn für Typ 0x2C9 zeichnet ("ENEMY", 0x14114135C), mit einer Attrappe:
// Balkenzustand an +0x2A0 (int[20] voll, float[20] Ausblenden ab +0xA0).
using HudFn = void (*)();
using BarFn = void (*)(void*, const char*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
HudFn g_hud = nullptr;
alignas(16) uint8_t g_fake_enemy[0x300];
alignas(16) uint8_t g_fake_bar[0x100];
// ---- Bildschirme mit Text (TS2-Redux-Issue #23)
//
// Computer-Bildschirme (Tastenfeld "Enter code:", Anzeigen mit Text) sind
// Rückrufe eines Objekts (0x1411F3E50, Platz 9), die ihren Inhalt in Pixeln
// oben links in den Bildpuffer zeichnen und dann ein Rechteck in Größe der
// Textur herauskopieren (0x14122C5D0 → 0x14122B384, CopySubresourceRegion).
// Rechtecke stehen in Konsolen-Pixeln (z. B. 0..128 × 0..64) und passen.
// Die Schriften des PC-Ports sind aber für die 1080 Zeilen der Oberfläche
// gemacht (2,25-mal so groß wie auf der Konsole mit 480 Zeilen); der Text
// ragt weit über die Textur hinaus, und auf dem Bildschirm erscheint nur sein
// Anfang, stark vergrößert. ts2.exe verkleinert für die Dauer dieser
// Rückrufe alle Schriftstile (0x14259C020, 24 × 0x28 Byte, Maßstab +8/+0xC)
// um 480/Höhe der Oberfläche.
struct FontStyle {
    int32_t font, pad;
    float sx, sy;
    uint8_t rest[0x18];
};
static_assert(sizeof(FontStyle) == 0x28, "Schriftstil");
constexpr int kFontStyles = 24;

class ConsoleFontScale {
public:
    ConsoleFontScale() {
        const int32_t* rect = *g_img->va<const int32_t*>(hf2::kHudRect);
        const int32_t h = rect ? rect[6] - rect[5] : 0;
        if (h <= 480) return;
        const float k = 480.0f / float(h);
        styles_ = g_img->va<FontStyle>(hf2::kFontStyles);
        for (int i = 0; i < kFontStyles; ++i) {
            saved_[i][0] = styles_[i].sx, saved_[i][1] = styles_[i].sy;
            styles_[i].sx *= k, styles_[i].sy *= k;
        }
    }
    ~ConsoleFontScale() {
        if (!styles_) return;
        for (int i = 0; i < kFontStyles; ++i) styles_[i].sx = saved_[i][0], styles_[i].sy = saved_[i][1];
    }

private:
    FontStyle* styles_ = nullptr;
    float saved_[kFontStyles][2] = {};
};

using ScreenFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t);
ScreenFn g_screen_fn[4] = {};
template <int K>
uint64_t screen_callback(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    static bool seen = false;
    if (!seen) seen = true, log("[hud] Bildschirm 0x%llX zeichnet zum ersten Mal\n", (unsigned long long)hf2::kScreenCallbacks[K]);
    ConsoleFontScale scale;
    return g_screen_fn[K](a, b, c, d);
}
const void* const kScreenWrappers[4] = {reinterpret_cast<const void*>(&screen_callback<0>),
                                        reinterpret_cast<const void*>(&screen_callback<1>),
                                        reinterpret_cast<const void*>(&screen_callback<2>),
                                        reinterpret_cast<const void*>(&screen_callback<3>)};

// Die Rückrufe werden mit lea r8,[rip+x] angemeldet; die Verweise zeigen
// danach auf Sprünge (jmp [rip]) in einer eigenen Seite des Abbilds.
bool install_screen_callbacks(const Ts2Image& img) {
    uint8_t* page = img.rva(hf2::kHudThunkRva);
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(page, &mbi, sizeof mbi) || mbi.State != MEM_RESERVE ||
        !VirtualAlloc(page, 0x1000, MEM_COMMIT, PAGE_EXECUTE_READWRITE))
        return false;
    int patched = 0;
    for (const auto& r : hf2::kScreenCallbackRefs) {
        uint8_t* lea = img.va(r.lea);
        int32_t disp;
        std::memcpy(&disp, lea + 3, 4);
        // lea r8, [rip+disp]: 4C 8D 05
        if (lea[0] != 0x4C || lea[1] != 0x8D || lea[2] != 0x05 || lea + 7 + disp != img.va(hf2::kScreenCallbacks[r.k]))
            continue;
        g_screen_fn[r.k] = reinterpret_cast<ScreenFn>(img.va(hf2::kScreenCallbacks[r.k]));
        uint8_t* thunk = page + 16 * r.k;
        uint8_t code[14] = {0xFF, 0x25, 0, 0, 0, 0};
        const uint64_t to = reinterpret_cast<uint64_t>(kScreenWrappers[r.k]);
        std::memcpy(code + 6, &to, 8);
        std::memcpy(thunk, code, sizeof code);
        const int32_t nd = int32_t(thunk - (lea + 7));
        DWORD old;
        VirtualProtect(lea + 3, 4, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(lea + 3, &nd, 4);
        VirtualProtect(lea + 3, 4, old, &old);
        ++patched;
    }
    DWORD old;
    VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), img.base, img.size);
    log("[hud] Bildschirme mit Text: %d von %zu Anmeldungen mit Konsolen-Schriftgröße\n", patched,
        sizeof hf2::kScreenCallbackRefs / sizeof hf2::kScreenCallbackRefs[0]);
    return patched > 0;
}

// Test (TS2_SCHIRMTEST=1): zeichnet nach dem HUD oben links, was der
// Tastenfeld-Bildschirm (0x141160FD4) in seine 128×64-Textur legt
// (Hintergrund, "Enter code:" in Schrift 4, Ziffern in Schrift 0xC), links
// mit der Schriftgröße des PC-Ports, rechts daneben (ab x = 200) verkleinert.
bool g_screentest = false;
using RectFn = void (*)(int32_t, int32_t, int32_t, int32_t, uint32_t);
using TextFn = void (*)(float, float, const char*, uint32_t);
using IntFn = int32_t (*)(int32_t);
using WidthFn = int32_t (*)(const char*);
void draw_screen_sample(int32_t x, bool console) {
    ConsoleFontScale* scale = console ? new ConsoleFontScale : nullptr;
    const char* const* texts = g_img->va<const char* const*>(hf2::kStringTables)[*g_img->va<int32_t>(hf2::kLanguage)];
    auto rect = reinterpret_cast<RectFn>(g_img->va<void>(0x14108A334));
    auto text = reinterpret_cast<TextFn>(g_img->va<void>(0x14121D6F0));
    auto font = reinterpret_cast<IntFn>(g_img->va<void>(0x14121BB24));
    auto width = reinterpret_cast<WidthFn>(g_img->va<void>(0x14121CCE0));
    rect(x, 0, x + 128, 32, 0x1E00007F);
    rect(x, 32, x + 128, 64, 0x7F3C147F);
    const int32_t old = font(4);
    const int32_t w = width(texts[1231]);
    if (w > 0x7C) font(0x11);
    text(float(x) + 2.0f, 0.0f, texts[1231], 0x7F50147F);
    font(0xC);
    text(float(x) + 20.0f, 34.0f, "8080", 0x7F);
    font(old);
    static int logged = 0;
    if (logged < 2) ++logged, log("[hud] Schirmtest: Breite von \"%s\" in Schrift 4: %d (Textur 128)\n", texts[1231], w);
    delete scale;
}
void draw_screentest() {
    draw_screen_sample(0, false);
    draw_screen_sample(200, true);
}

void hud_bosstest() {
    g_hud();
    if (g_screentest) draw_screentest();
    if (!*reinterpret_cast<uint8_t**>(g_fake_enemy + 0x2A0)) return;
    if (*g_img->va<int32_t>(hf2::kGameMode) != 4) return;
    const int32_t* rect = *g_img->va<const int32_t*>(hf2::kHudRect);  // +0 links, +4 rechts, +0x14 oben
    if (!rect) return;
    const int32_t ml = *g_img->va<int32_t>(hf2::kHudMarginL), mr = *g_img->va<int32_t>(hf2::kHudMarginR);
    const float top = float(rect[5] + *g_img->va<int32_t>(hf2::kHudMarginT));
    const float w = float(rect[1] - rect[0] - ml - mr), left = float(ml + rect[0]);
    const char* const* texts = g_img->va<const char* const*>(hf2::kStringTables)[*g_img->va<int32_t>(hf2::kLanguage)];
    reinterpret_cast<BarFn>(g_img->va<void>(hf2::kEnemyBar))(g_fake_enemy, texts[1674], int32_t(w * 0.7f + left),
                                                             int32_t(top + 20.0f), int32_t(w * 0.95f + left),
                                                             int32_t(top + 34.0f), 0x7F, 0x6E);
}

}  // namespace

void hudfix_install(const Ts2Image& img, const std::filesystem::path& ini) {
    (void)ini;
    g_img = &img;
    if (!install_tex_list_guard(img)) log("[hud] Texturliste 0x14122B41C: andere Bytes, ohne Grenze\n");
    if (!install_tex_recycling(img)) log("[hud] Texturplätze: Stellen nicht gefunden, alte D3D-Texturen bleiben liegen\n");
    if (!GetEnvironmentVariableW(L"TS2_SCHIRM_ALT", nullptr, 0) && !install_screen_callbacks(img))  // Test: wie bisher
        log("[hud] Bildschirme mit Text: nicht umgestellt\n");
    if (!GetEnvironmentVariableW(L"TS2_BALKEN_ALT", nullptr, 0)) {  // Test: Balken wie bisher
        g_quad = reinterpret_cast<QuadFn>(
            hook_call(img, hf2::kEnemyBarQuadCall, hf2::kHudQuad, reinterpret_cast<const void*>(&enemy_bar_quad)));
        if (!g_quad) log("[hud] Gegnerbalken: Aufruf 0x141140906 nicht gefunden\n");
    }
    if (GetEnvironmentVariableW(L"TS2_HUDPROBE", nullptr, 0)) {
        AddVectoredExceptionHandler(1, probe_exception);
        g_model_load = reinterpret_cast<ModelFn>(hook_call(img, hf2::kCharModelLoadCall, hf2::kCharModelLoad,
                                                           reinterpret_cast<const void*>(&model_load_probe)));
    }
    wchar_t v[16] = {};
    g_screentest = GetEnvironmentVariableW(L"TS2_SCHIRMTEST", nullptr, 0) > 0;
    const bool bosstest = GetEnvironmentVariableW(L"TS2_BOSSTEST", v, 16) > 0;
    if (bosstest) {
        const int full = _wtoi(v);
        *reinterpret_cast<uint8_t**>(g_fake_enemy + 0x2A0) = g_fake_bar;
        for (int i = 0; i < 20; ++i) reinterpret_cast<int32_t*>(g_fake_bar)[i] = full >= 0 ? i < full : i >= 20 + full;
        log("[hud] Test: Gegnerbalken mit %d von 20 Segmenten\n", full);
    }
    if (bosstest || g_screentest) {
        g_hud = reinterpret_cast<HudFn>(
            hook_call(img, hf2::kHudDrawCall, hf2::kHudDraw, reinterpret_cast<const void*>(&hud_bosstest)));
        if (!g_hud) log("[hud] Test: HUD-Aufruf nicht gefunden\n");
    }
}

}  // namespace host
