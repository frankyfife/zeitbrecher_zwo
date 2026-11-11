#include "logofix.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "hooks.h"
#include "log.h"

namespace host {

namespace {

const Ts2Image* g_img = nullptr;
ID3D11Device* g_dev = nullptr;

// ---- Abhilfe

constexpr uint64_t kCallParticles = 0x1410B586F;  // call 0x14112FA10 (Teilchen zeichnen) in 0x1410B5800
constexpr uint64_t kParticles = 0x14112FA10;
constexpr uint64_t kCallGlow = 0x1410B58EF;       // call 0x14108A41C (Sprite ts2glow)
constexpr uint64_t kSprite = 0x14108A41C;
constexpr uint64_t kAlphaRef = 0x14259D554;   // alphaRefMin für die nächsten Zeichnungen (0x14122A620)
constexpr float kXboxAlphaRef = 127.0f / 255.0f;  // Xbox: ALPHAFUNC GREATER, ALPHAREF 0x7E

using ParticlesFn = void(__fastcall*)(void*, int);
using SpriteFn = void(__fastcall*)(int, int, int, int, int, int, int, int, uint32_t);
ParticlesFn g_particles = nullptr;
SpriteFn g_sprite = nullptr;

// Feuerteilchen: Alpha-Test wie die Xbox-Art 7 (ihre Größe in Konsolen-
// Bildpunkten richtet spritefix.cpp für alle Teilchen)
void __fastcall particles(void* sys, int arg) {
    float* aref = g_img->va<float>(kAlphaRef);
    const float old_aref = *aref;
    *aref = kXboxAlphaRef;
    g_particles(sys, arg);
    *aref = old_aref;
}

// Schein: Alpha-Test bleibt wie auf der Xbox an
void __fastcall glow(int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1, uint32_t color) {
    float* aref = g_img->va<float>(kAlphaRef);
    const float old_aref = *aref;
    *aref = kXboxAlphaRef;
    g_sprite(x0, y0, x1, y1, u0, v0, u1, v1, color);
    *aref = old_aref;
}

// ---- FREE RADICAL / EIDOS (Xbox PAL 0x88D3B–0x88E2C; im PC-Code entfernt)
//
// Die Xbox setzt nach dem Schein Mischart 3 (SRC_ALPHA/ONE, Schreibmaske
// weiter nur RGB, Alpha-Test 0x7E weiter an) und zeichnet ab Sekunde 1 der
// Uhr 0x35C888 (PC 0x142F12728) zwei Sprites mit Farbe 0x320A0A00 | a,
// a = min(Uhr − 1, 1) · 115: misc/frd bei (48,370)–(134,420), misc/eidos
// bei (420,370)–(580,420) im 640×480-Bild. Der PC zeichnet die übrigen
// Vollbild-Elemente dieser Funktion mit ×3/×2,25 (Abblend-Rechtecke
// 0x14108A334(0,0,0x780,0x438), Xbox 0x280×0x1E0), also ebenso.
constexpr uint64_t kCallBlend3 = 0x1410B58F9;  // call 0x14122A5BC (Mischart 3) nach dem Schein
constexpr uint64_t kSetBlend = 0x14122A5BC;
constexpr uint64_t kMiscTex = 0x1410D4150;     // Nummer der misc-Textur i
constexpr uint64_t kBindTex = 0x141088C9C;     // Textur für 2D setzen
constexpr uint64_t kTexBank = 0x14259D5E0;     // **Beschreiber, je 0x38, +0x26/+0x2A Breite/Höhe
constexpr uint64_t kIntroClock = 0x142F12728;  // Sekunden seit Beginn des Bildschirms
constexpr int kTexFrd = 0x74, kTexEidos = 0x73;

using SetBlendFn = int(__fastcall*)(int);
using MiscTexFn = int(__fastcall*)(int);
using BindTexFn = void(__fastcall*)(int);
SetBlendFn g_set_blend = nullptr;
bool g_logos_logged = false;

void company_logo(int index, int x0, int y0, int x1, int y1, uint32_t color) {
    const int tex = reinterpret_cast<MiscTexFn>(g_img->va<uint8_t>(kMiscTex))(index);
    if (tex < 0) {
        if (!g_logos_logged) log("Brennendes Logo: misc-Textur %d nicht geladen\n", index);
        return;
    }
    reinterpret_cast<BindTexFn>(g_img->va<uint8_t>(kBindTex))(tex);
    const uint8_t* d = *g_img->va<uint8_t*>(kTexBank) ? *reinterpret_cast<uint8_t* const*>(*g_img->va<uint8_t*>(kTexBank))
                                                      : nullptr;
    if (!d) return;
    d += size_t(tex) * 0x38;
    const int w = *reinterpret_cast<const uint16_t*>(d + 0x26), h = *reinterpret_cast<const uint16_t*>(d + 0x2A);
    g_sprite(x0 * 3, (y0 * 9) / 4, x1 * 3, (y1 * 9) / 4, 0, 0, w << 4, h << 4, color);
}

int __fastcall blend3(int mode) {
    const int old = g_set_blend(mode);
    const float t = *g_img->va<float>(kIntroClock);
    if (t > 1.0f) {
        const int a = int((t - 1.0f < 1.0f ? t - 1.0f : 1.0f) * 115.0f);
        const uint32_t color = 0x320A0A00u | uint32_t(a & 0xFF);
        float* aref = g_img->va<float>(kAlphaRef);
        const float old_aref = *aref;
        *aref = kXboxAlphaRef;  // Xbox: Alpha-Test der Art 7 gilt noch
        company_logo(kTexFrd, 0x30, 0x172, 0x86, 0x1A4, color);
        company_logo(kTexEidos, 0x1A4, 0x172, 0x244, 0x1A4, color);
        *aref = old_aref;
        g_logos_logged = true;
    }
    return old;
}

// ---- Rechtehinweis vor dem brennenden Logo (Xbox PAL 0xDD290, PC 0x1410D6048)
//
// Level 0x67 (Titel-Level l_103) zeichnet in 0x1410D6048 (Aufruf
// 0x14114D2C0) den Hinweis, solange das nächste Level noch nicht geladen ist.
// Xbox (Zweig "kein Sprachmenü", [0x386790] != 1): igmtex über das ganze Bild
// (0x0014327F), ts2loading 640×480 (0x4F4F4F3F), ts2newlogo (40,0)–(600,256),
// frd (45,300)–(182,364), eidos (420,300)–(580,364), alle 0x7F7F7F7F, dann
// Text 1712 (Stil 0x10) mittig, Unterkante 0x37 über dem Bildrand. Der
// Ladebalken kommt aus der Ladeanzeige 0xDCE90 (Rechteck 0xDCD90(60,250,392,15)
// in 512×448-Einheiten = (75,263)+490×16 bei 640×480; Hintergrund 0x2A18180F
// zwei Punkte größer, Füllung 0x141E3C7F).
// Der PC-Port zeichnet dasselbe, aber igmtex schreibt Tiefe und verdeckt alles
// danach (gleiches z), die Logos stehen in 640×480-Punkten oben links, der
// Text 0xF statt 0x37 über dem Rand, kein Balken; der Bildschirm ist nach
// wenigen Bildern weg.
constexpr uint64_t kCallLegal = 0x14114D2C0;   // call 0x1410D6048
constexpr uint64_t kLegal = 0x1410D6048;
// Level-Zustand 0x142EE3DF0 (0 laden, 1 laufen, 2 beenden), je Takt in
// 0x14107CF44 (Aufruf 0x14122C0F6); in Zustand 1 wechselt er auf 2, sobald das
// Level sein Ende meldet (0x1424E2420 oder 0x142EE3DF8), sonst Takt 0x14107CAA8.
// Gezeichnet wird nur in Zustand 1.
constexpr uint64_t kCallTick = 0x14122C0F6;    // call 0x14107CF44
constexpr uint64_t kTick = 0x14107CF44;
constexpr uint64_t kLevelState = 0x142EE3DF0;
constexpr uint64_t kLevelEnd1 = 0x1424E2420, kLevelEnd2 = 0x142EE3DF8;
constexpr uint64_t kRunTick = 0x14107CAA8;
constexpr uint64_t kTicked = 0x142EE3E14;      // 1: Takt gelaufen, Zeichnen erlaubt
constexpr uint64_t kLevel = 0x143F27F54;
constexpr uint64_t kLangMenu = 0x142F16138;    // 1: Sprachwahl statt Hinweis
constexpr uint64_t kSetDepth = 0x14122A620;    // Tiefen-/Alpha-Test-Art
constexpr uint64_t kFillRect = 0x14108A334;    // (x0,y0,x1,y1,Farbe)
constexpr uint64_t kTextStyle = 0x14121BB24;   // Stil setzen, liefert alten
constexpr uint64_t kTextSize = 0x14121BC30;    // (&b,&h,text)
constexpr uint64_t kTextDraw = 0x14121DA28;    // (&x,&y,text,Farbe)
constexpr uint64_t kTexts = 0x1424E2430;       // Zeiger je Sprache auf char*[2770]
constexpr uint64_t kLanguage = 0x142EE39C0;
constexpr int kTexIgm = 0x75, kTexLoading = 0x68, kTexNewLogo = 0x71;
constexpr uint64_t kStyles = 0x14259C020;      // 24 Schriftstile je 0x28, +8/+0xC Maßstab
constexpr float kTextHeight = 0.62f;

using VoidFn = void(__fastcall*)();
using IntIntFn = int(__fastcall*)(int);
using FillFn = void(__fastcall*)(int, int, int, int, uint32_t);
using SizeFn = void(__fastcall*)(int*, int*, const char*);
using DrawTextFn = void(__fastcall*)(int*, int*, const char*, uint32_t);
VoidFn g_legal_orig = nullptr, g_tick_orig = nullptr;
int g_legal_ms = 2000;                         // Mindestdauer
LARGE_INTEGER g_legal_t0{}, g_freq{};
bool g_legal_done = false;

template <class F> F fn(uint64_t va) { return reinterpret_cast<F>(g_img->va<uint8_t>(va)); }

// Textur binden, Größe in 1/16 Pixel liefern; false: nicht geladen
bool bind_misc(int index, int* w16, int* h16) {
    const int tex = fn<IntIntFn>(kMiscTex)(index);
    if (tex < 0) return false;
    fn<BindTexFn>(kBindTex)(tex);
    const uint8_t* bank = *g_img->va<uint8_t*>(kTexBank);
    const uint8_t* d = bank ? *reinterpret_cast<const uint8_t* const*>(bank) : nullptr;
    if (!d) return false;
    d += size_t(tex) * 0x38;
    *w16 = *reinterpret_cast<const uint16_t*>(d + 0x26) << 4;
    *h16 = *reinterpret_cast<const uint16_t*>(d + 0x2A) << 4;
    return true;
}

void sprite(int index, int x0, int y0, int x1, int y1, uint32_t color) {
    int w = 0, h = 0;
    if (bind_misc(index, &w, &h)) fn<SpriteFn>(kSprite)(x0, y0, x1, y1, 0, 0, w, h, color);
}

// 640×480 der Xbox -> 1920×1080, wie die übrigen 2D-Elemente des Startbilds
int sx(int x) { return x * 3; }
int sy(int y) { return y * 9 / 4; }

double legal_elapsed_ms() {
    if (!g_legal_t0.QuadPart) return 0;
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart - g_legal_t0.QuadPart) * 1000.0 / double(g_freq.QuadPart);
}

// Weiter mit Enter, Leertaste, Esc, Linksklick oder A/Start am Pad (nur mit Fokus)
bool legal_skip_pressed() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) return false;
    for (int vk : {VK_RETURN, VK_SPACE, VK_ESCAPE, VK_LBUTTON})
        if (GetAsyncKeyState(vk) & 0x8000) return true;
    using XInputGetStateFn = DWORD(WINAPI*)(DWORD, void*);
    static XInputGetStateFn xi = [] {
        HMODULE m = LoadLibraryW(L"xinput1_4.dll");
        return m ? reinterpret_cast<XInputGetStateFn>(GetProcAddress(m, "XInputGetState")) : nullptr;
    }();
    struct {
        DWORD packet;
        WORD buttons;
        BYTE lt, rt;
        SHORT lx, ly, rx, ry;
    } st{};
    for (DWORD i = 0; xi && i < 4; ++i)
        if (xi(i, &st) == 0 && (st.buttons & (0x1000 | 0x0010))) return true;  // A, Start
    return false;
}

void __fastcall legal_screen() {
    if (*g_img->va<int32_t>(kLangMenu) == 1) return g_legal_orig();  // Sprachwahl: wie bisher
    if (!g_legal_t0.QuadPart) QueryPerformanceCounter(&g_legal_t0), log("Rechtehinweis: Anzeige beginnt\n");
    if (!g_legal_done && (legal_elapsed_ms() >= g_legal_ms || legal_skip_pressed())) g_legal_done = true;
    const float f = g_legal_ms > 0 ? std::min(1.0f, float(legal_elapsed_ms() / g_legal_ms)) : 1.0f;
    const int old_depth = fn<IntIntFn>(kSetDepth)(0);  // ohne Tiefentest wie auf der Xbox
    fn<SetBlendFn>(kSetBlend)(2);
    {  // igmtex: wie im Original nur das erste Sechzehntel (Texturkoordinaten ohne <<4)
        int w = 0, h = 0;
        if (bind_misc(kTexIgm, &w, &h)) fn<SpriteFn>(kSprite)(0, 0, 1920, 1080, 0, 0, w >> 4, h >> 4, 0x0014327F);
    }
    // Hintergrund 4:3 unverzerrt in der Mitte; die Seiten zeigt der Verlauf darunter
    sprite(kTexLoading, 240, 0, 1680, 1080, 0x4F4F4F3F);
    sprite(kTexNewLogo, sx(40), sy(0), sx(600), sy(256), 0x7F7F7F7F);
    sprite(kTexFrd, sx(45), sy(300), sx(182), sy(364), 0x7F7F7F7F);
    sprite(kTexEidos, sx(420), sy(300), sx(580), sy(364), 0x7F7F7F7F);
    // Rechtetext (Text 1712, Stil 0x10), Unterkante 0x37 Konsolen-Punkte über dem Rand
    const int lang = *g_img->va<int32_t>(kLanguage);
    const char* const* table = g_img->va<const char* const*>(kTexts)[lang];
    if (table && table[1712]) {
        char buf[1024];
        strncpy(buf, table[1712], sizeof buf - 1);
        buf[sizeof buf - 1] = 0;
        const int old_style = fn<IntIntFn>(kTextStyle)(0x10);
        // Die PC-Schrift ist bei gleicher Breite 1,6-mal so hoch wie die Xbox-Schrift
        // auf 16:9 gestreckt (gemessen: 4 Zeilen 175 statt 108 Punkte); Höhe wie Xbox
        float* yscale = g_img->va<float>(kStyles + 0x10 * 0x28 + 0xC);
        const float old_yscale = *yscale;
        *yscale = old_yscale * kTextHeight;
        int w = 0, h = 0;
        fn<SizeFn>(kTextSize)(&w, &h, buf);
        int x = 960 - w / 2, y = 1080 - h - sy(0x37);
        fn<SetBlendFn>(kSetBlend)(2);
        fn<DrawTextFn>(kTextDraw)(&x, &y, buf, 0x7F7F7F7F);
        *yscale = old_yscale;
        fn<IntIntFn>(kTextStyle)(old_style);
    }
    // Die Xbox lädt, während das Bild noch einblendet: alles bis hierher steht bei
    // 24 % Helligkeit (gemessen: weißer Text 59/255), der Balken voll darüber.
    // Einblenden aus Schwarz in den ersten 300 ms, dann wie dort.
    {
        const double t = legal_elapsed_ms();
        const float keep = t < 300 ? float(0.24 * t / 300.0) : 0.24f;
        const uint32_t a = uint32_t(std::clamp((1.0f - keep) * 127.5f, 0.0f, 127.0f) + 0.5f);
        fn<SetBlendFn>(kSetBlend)(2);
        fn<FillFn>(kFillRect)(0, 0, 1920, 1080, 0x00000000u | a);
    }
    // Ladebalken wie die Xbox-Ladeanzeige; läuft über die Mindestdauer voll
    fn<SetBlendFn>(kSetBlend)(2);
    fn<FillFn>(kFillRect)(sx(73), sy(261), sx(567), sy(281), 0x180F0F7Fu);  // Xbox 0x2A18180F, dort deckend und ohne Verdopplung gemessen (50,30,31)
    if (f > 0) fn<FillFn>(kFillRect)(sx(75), sy(263), sx(75) + int(sx(490) * f), sy(279), 0x141E3C7Fu);
    // Probe TS2_UMLAUTTEST=1: Umlaute in den Stilen der Schriften book und booklight
    static const bool umlaut_test = GetEnvironmentVariableW(L"TS2_UMLAUTTEST", nullptr, 0) > 0;
    if (umlaut_test) {
        fn<FillFn>(kFillRect)(0, 0, 1920, 560, 0x0000007Fu);
        const int styles[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x16, 0x0C};
        int y = 10;
        for (int st : styles) {
            const int old = fn<IntIntFn>(kTextStyle)(st);
            char line[64];
            std::snprintf(line, sizeof line, "%02X o\xF6 a\xE4 u\xFC O\xD6 A\xC4 U\xDC dazugeh\xF6rigen", st);
            int x = 20, yy = y, w = 0, h = 0;
            fn<SizeFn>(kTextSize)(&w, &h, line);
            fn<DrawTextFn>(kTextDraw)(&x, &yy, line, 0x7F7F7F7F);
            fn<IntIntFn>(kTextStyle)(old);
            y += h + 12;
        }
    }
    fn<IntIntFn>(kSetDepth)(old_depth);
}

// Takt: Das Titel-Level meldet sein Ende, sobald geladen ist; bis zur
// Mindestdauer läuft es weiter (Zustand 1), damit der Hinweis gezeichnet wird.
void __fastcall level_tick() {
    if (!g_legal_done && g_legal_t0.QuadPart && (legal_elapsed_ms() >= g_legal_ms || legal_skip_pressed()))
        g_legal_done = true;
    if (!g_legal_done && g_legal_t0.QuadPart && *g_img->va<int32_t>(kLevel) == 0x67 &&
        *g_img->va<int32_t>(kLevelState) == 1 &&
        (*g_img->va<int32_t>(kLevelEnd1) != 0 || *g_img->va<int32_t>(kLevelEnd2) != 0)) {
        fn<VoidFn>(kRunTick)();
        *g_img->va<int32_t>(kTicked) = 1;
        return;
    }
    g_tick_orig();
}

void install_legal(const Ts2Image& img) {
    wchar_t v[16] = {};
    if (GetEnvironmentVariableW(L"TS2_RECHTEHINWEIS", v, 16) > 0 && v[0] == L'0') return;
    QueryPerformanceFrequency(&g_freq);
    // Mindestdauer: TS2_RECHTE_MS; in Testläufen (--takte) 0, damit Skripte ihre Takte behalten
    const bool scripted = wcsstr(GetCommandLineW(), L"--takte") != nullptr;
    g_legal_ms = scripted ? 0 : 2000;
    if (GetEnvironmentVariableW(L"TS2_RECHTE_MS", v, 16) > 0) g_legal_ms = _wtoi(v);
    g_legal_orig = reinterpret_cast<VoidFn>(hook_call(img, kCallLegal, kLegal, reinterpret_cast<const void*>(&legal_screen)));
    g_tick_orig = reinterpret_cast<VoidFn>(hook_call(img, kCallTick, kTick, reinterpret_cast<const void*>(&level_tick)));
    if (g_legal_orig && g_tick_orig)
        log("Rechtehinweis wie Xbox (Hintergrund, Logos, Ladebalken), mindestens %d ms\n", g_legal_ms);
}

// ---- Probe (TS2_LOGOPROBE=<n>)

int g_probe = -1;  // beim n-ten Schein-Bild Bildpuffer vor/nach dem Schein schreiben
int g_glow_n = 0;  // gezählte Schein-Zeichnungen
int g_parts = 0;   // Teilchen seit dem letzten Schein

bool blend_is(ID3D11DeviceContext* c, D3D11_BLEND src, D3D11_BLEND dst, UINT8* mask) {
    ID3D11BlendState* st = nullptr;
    FLOAT f[4];
    UINT sm = 0;
    c->OMGetBlendState(&st, f, &sm);
    if (!st) return false;
    D3D11_BLEND_DESC d;
    st->GetDesc(&d);
    st->Release();
    const auto& r = d.RenderTarget[0];
    if (mask) *mask = r.RenderTargetWriteMask;
    return r.BlendEnable && r.SrcBlend == src && r.DestBlend == dst;
}

bool bound_size(ID3D11DeviceContext* c, UINT* w, UINT* h) {
    ID3D11ShaderResourceView* srv = nullptr;
    c->PSGetShaderResources(0, 1, &srv);
    if (!srv) return false;
    ID3D11Resource* r = nullptr;
    srv->GetResource(&r);
    srv->Release();
    ID3D11Texture2D* t = nullptr;
    if (r) r->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t)), r->Release();
    if (!t) return false;
    D3D11_TEXTURE2D_DESC d;
    t->GetDesc(&d);
    t->Release();
    *w = d.Width;
    *h = d.Height;
    return true;
}

std::wstring exe_dir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    return s.substr(0, s.find_last_of(L"\\/") + 1);
}

// Bildpuffer lesen: Alpha als PGM, Farbe als PPM (neben ts2.exe)
void dump_target(ID3D11DeviceContext* c, const wchar_t* tag) {
    ID3D11RenderTargetView* rtv = nullptr;
    c->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv) return;
    ID3D11Resource* r = nullptr;
    rtv->GetResource(&r);
    rtv->Release();
    ID3D11Texture2D* t = nullptr;
    if (r) r->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t)), r->Release();
    if (!t) return;
    D3D11_TEXTURE2D_DESC d;
    t->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    d.MipLevels = 1;
    d.ArraySize = 1;
    ID3D11Texture2D* st = nullptr;
    if (d.Format == DXGI_FORMAT_R8G8B8A8_UNORM && SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &st))) {
        c->CopySubresourceRegion(st, 0, 0, 0, 0, t, 0, nullptr);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(c->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
            const std::wstring base = exe_dir() + L"logo_" + tag;
            FILE* fa = _wfopen((base + L"_alpha.pgm").c_str(), L"wb");
            FILE* fc = _wfopen((base + L"_farbe.ppm").c_str(), L"wb");
            if (fa) fprintf(fa, "P5\n%u %u\n255\n", d.Width, d.Height);
            if (fc) fprintf(fc, "P6\n%u %u\n255\n", d.Width, d.Height);
            std::vector<uint8_t> a(d.Width), rgb(size_t(d.Width) * 3);
            for (UINT y = 0; y < d.Height; ++y) {
                const uint8_t* s = static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch;
                for (UINT x = 0; x < d.Width; ++x) {
                    a[x] = s[x * 4 + 3];
                    rgb[x * 3] = s[x * 4];
                    rgb[x * 3 + 1] = s[x * 4 + 1];
                    rgb[x * 3 + 2] = s[x * 4 + 2];
                }
                if (fa) fwrite(a.data(), 1, a.size(), fa);
                if (fc) fwrite(rgb.data(), 1, rgb.size(), fc);
            }
            if (fa) fclose(fa);
            if (fc) fclose(fc);
            c->Unmap(st, 0);
            log("[logo] Bild %d geschrieben: %ls_*\n", g_glow_n, base.c_str());
        }
        st->Release();
    }
    t->Release();
}

}  // namespace

bool logofix_init(const Ts2Image& img, ID3D11Device* dev) {
    g_img = &img;
    g_dev = dev;
    wchar_t v[16] = {};
    if (GetEnvironmentVariableW(L"TS2_LOGOPROBE", v, 16) > 0) g_probe = _wtoi(v);
    if (GetEnvironmentVariableW(L"TS2_LOGOFIX", v, 16) > 0 && v[0] == L'0') {
        log("Brennendes Logo: unverändert (TS2_LOGOFIX=0)\n");
    } else {
        g_particles = reinterpret_cast<ParticlesFn>(hook_call(img, kCallParticles, kParticles, reinterpret_cast<const void*>(&particles)));
        if (g_particles) g_sprite = reinterpret_cast<SpriteFn>(hook_call(img, kCallGlow, kSprite, reinterpret_cast<const void*>(&glow)));
        if (g_particles && g_sprite)
            log("Brennendes Logo: Alpha-Test wie Xbox\n");
        // FREE RADICAL / EIDOS wie auf der Xbox (TS2_FIRMENLOGOS=0: weglassen)
        if (g_sprite && !(GetEnvironmentVariableW(L"TS2_FIRMENLOGOS", v, 16) > 0 && v[0] == L'0')) {
            g_set_blend = reinterpret_cast<SetBlendFn>(
                hook_call(img, kCallBlend3, kSetBlend, reinterpret_cast<const void*>(&blend3)));
            if (g_set_blend) log("Brennendes Logo: FREE RADICAL und EIDOS wie auf der Xbox\n");
        }
    }
    install_legal(img);
    return g_probe >= 0;
}

bool logofix_before(ID3D11DeviceContext* c) {
    UINT8 mask = 0;
    UINT w = 0, h = 0;
    if (blend_is(c, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE, &mask) && mask == D3D11_COLOR_WRITE_ENABLE_ALPHA) {
        ++g_parts;
        return false;
    }
    if (!blend_is(c, D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_ONE, &mask) || !bound_size(c, &w, &h) || w != 512 ||
        h != 256)
        return false;
    ++g_glow_n;
    const bool now = g_glow_n == g_probe;
    if (now) {
        log("[logo] Schein %d: %d Teilchen davor\n", g_glow_n, g_parts);
        dump_target(c, L"vor");
    }
    g_parts = 0;
    return now;
}

void logofix_after(ID3D11DeviceContext* c) { dump_target(c, L"nach"); }

}  // namespace host
