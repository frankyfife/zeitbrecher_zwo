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
constexpr uint64_t kCamera = 0x143F3A870;     // Zeiger auf die Kamera, +0x27C Größenfaktor der Teilchen
constexpr uint64_t kScreen = 0x143F0F508;     // Zeiger auf die Sicht, +0x18 Höhe in Bildpunkten
constexpr uint64_t kLists = 0x142F3A210;      // 17 Teilchenlisten (je Klasse), verkettet über +0x68
constexpr int kListCount = 17;
constexpr float kXboxAlphaRef = 127.0f / 255.0f;  // Xbox: ALPHAFUNC GREATER, ALPHAREF 0x7E

using ParticlesFn = void(__fastcall*)(void*, int);
using SpriteFn = void(__fastcall*)(int, int, int, int, int, int, int, int, uint32_t);
ParticlesFn g_particles = nullptr;
SpriteFn g_sprite = nullptr;

// Teilchen in Bildpunkten einer 480 Zeilen hohen Konsolen-Sicht: auf die
// Sichthöhe des PC-Ports vergrößern
float screen_scale() {
    const int32_t* vp = *g_img->va<int32_t*>(kScreen);
    const int h = vp ? vp[6] : 0;
    return h > 0 ? float(h) / 480.0f : 1.0f;
}

void __fastcall particles(void* sys, int arg) {
    float* aref = g_img->va<float>(kAlphaRef);
    const float old_aref = *aref;
    *aref = kXboxAlphaRef;
    const float k = screen_scale();
    uint8_t* cam = *g_img->va<uint8_t*>(kCamera);
    float* size = cam ? reinterpret_cast<float*>(cam + 0x27C) : nullptr;
    const float old_size = size ? *size : 0.0f;
    if (size) *size = old_size * k;
    // Obergrenze je Teilchen (+0x40) ebenso
    std::vector<std::pair<float*, float>> caps;
    uint8_t** lists = g_img->va<uint8_t*>(kLists);
    for (int i = 0; i < kListCount; ++i)
        for (uint8_t* p = lists[i]; p && caps.size() < 4096; p = *reinterpret_cast<uint8_t**>(p + 0x68)) {
            float* cap = reinterpret_cast<float*>(p + 0x40);
            caps.emplace_back(cap, *cap);
            *cap *= k;
        }
    g_particles(sys, arg);
    for (auto& [cap, v] : caps) *cap = v;
    if (size) *size = old_size;
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
            log("Brennendes Logo: Teilchen in Konsolengröße (Bildhöhe/480), Alpha-Test wie Xbox\n");
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
