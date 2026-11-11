#include "logofix.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
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
