#include "shadowfix.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <utility>

#include "log.h"

namespace host {

namespace {

// Eingänge wie TS2s Pixel-Shader (Position, Normale, Texturkoordinate, Farbe)
const char kShader[] = R"(
Texture2D tex : register(t0);
SamplerState smp : register(s0);
cbuffer Shadow : register(b13) { float strength; float3 pad; };
struct V {
    float4 pos : SV_POSITION;
    float3 n : NORMAL;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};
float4 ps(V i) : SV_Target {
    float4 t = tex.Sample(smp, i.uv);
    return float4(t.rgb, saturate(t.a * strength));
}
)";

// TS2s spec-Bits (Shader ts.vp/ts.pp, effectfix.cpp)
constexpr uint32_t kSpecColor = 0x3;          // Eckenfarbe benutzen
constexpr uint32_t kSpecVsLight = 0x200;      // Licht im Vertex-Shader (Alpha +1 je Lampe)
constexpr uint32_t kSpecLevel = 0x20000000;   // Levelgeometrie, Licht im Pixel-Shader
constexpr uint32_t kSpecScreen = 0x20;        // schon projiziert (2D)

ID3D11Device* g_dev = nullptr;
ID3D11PixelShader* g_ps = nullptr;
ID3D11Buffer* g_cb = nullptr;
const uint32_t* g_spec = nullptr;
std::map<ID3D11Texture2D*, bool> g_is_shadow;  // je Textur einmal geprüft
ID3D11PixelShader* g_saved_ps = nullptr;
ID3D11Buffer* g_saved_cb = nullptr;

// Schattentextur: 4×4 bis 256×256, Farbe überall schwarz, Alpha formt den
// Fleck (irgendwo durchsichtig, irgendwo mindestens 0,19 deckend). Dazu
// gehören der 16×16-Klecks (Alpha bis 0,62) und die Schatten einzelner
// Objekte (Kistenumriss, Tisch, Stuhl, Fass ...).
bool check_texture(ID3D11DeviceContext* c, ID3D11Texture2D* tex) {
    D3D11_TEXTURE2D_DESC d;
    tex->GetDesc(&d);
    if (d.Width < 4 || d.Height < 4 || d.Width > 256 || d.Height > 256 || d.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
        d.SampleDesc.Count != 1 || !(d.BindFlags & D3D11_BIND_SHADER_RESOURCE))
        return false;
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    d.MipLevels = 1;
    d.ArraySize = 1;
    ID3D11Texture2D* st = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&d, nullptr, &st))) return false;
    c->CopySubresourceRegion(st, 0, 0, 0, 0, tex, 0, nullptr);
    bool shadow = false;
    int rgb_max = 0, a_max = 0, a_min = 255;
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(c->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
        for (UINT y = 0; y < d.Height; ++y)
            for (UINT x = 0; x < d.Width; ++x) {
                const uint8_t* p = static_cast<const uint8_t*>(m.pData) + y * m.RowPitch + x * 4;
                rgb_max = std::max({rgb_max, int(p[0]), int(p[1]), int(p[2])});
                a_max = std::max(a_max, int(p[3]));
                a_min = std::min(a_min, int(p[3]));
            }
        c->Unmap(st, 0);
        shadow = rgb_max <= 8 && a_max >= 48 && a_min <= 16;
    }
    st->Release();
    if (shadow)
        log("Schatten: Textur %p (%ux%u, Alpha %d..%d) ist ein Schattenfleck\n", static_cast<void*>(tex), d.Width,
            d.Height, a_min, a_max);
    return shadow;
}

}  // namespace

bool shadowfix_init(ID3D11Device* dev, float strength, const uint32_t* spec) {
    if (strength <= 0.0f) return false;
    g_dev = dev;
    g_spec = spec;
    ID3DBlob *code = nullptr, *err = nullptr;
    if (FAILED(D3DCompile(kShader, sizeof kShader - 1, "shadowfix", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &code, &err))) {
        log("Schatten: Shader nicht übersetzbar: %s\n", err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
        if (err) err->Release();
        return false;
    }
    if (err) err->Release();
    // Mitte des 16×16-Kleckses hat Alpha ≈ 0,62: auf die gewünschte Deckkraft
    // bringen; andere Schattentexturen im selben Verhältnis
    const float data[4] = {strength / 0.62f, 0, 0, 0};
    D3D11_BUFFER_DESC bd{sizeof data, D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    D3D11_SUBRESOURCE_DATA sd{data, 0, 0};
    const bool ok = SUCCEEDED(dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_ps)) &&
                    SUCCEEDED(dev->CreateBuffer(&bd, &sd, &g_cb));
    code->Release();
    if (ok) log("Schatten: feste Deckkraft %.2f statt Eckenfarbe/Licht\n", strength);
    return ok;
}

bool shadowfix_before(ID3D11DeviceContext* c) {
    if (!g_ps) return false;
    // nur Objekte und Figuren: Levelgeometrie (gebackene Schatten, Eckenfarbe
    // stimmt) und 2D (Fadenkreuz, Zielfernrohr) bleiben, wie sie sind
    const uint32_t spec = g_spec ? *g_spec : 0;
    if (spec & (kSpecLevel | kSpecScreen)) return false;
    if (!(spec & (kSpecColor | kSpecVsLight))) return false;
    ID3D11ShaderResourceView* srv = nullptr;
    c->PSGetShaderResources(0, 1, &srv);
    if (!srv) return false;
    ID3D11Resource* r = nullptr;
    srv->GetResource(&r);
    srv->Release();
    ID3D11Texture2D* tex = nullptr;
    if (r) {
        r->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
        r->Release();
    }
    if (!tex) return false;
    auto it = g_is_shadow.find(tex);
    const bool shadow = it != g_is_shadow.end() ? it->second : (g_is_shadow[tex] = check_texture(c, tex));
    if (shadow) {
        // jede Paarung Textur/spec einmal melden (Befund, Fehlersuche)
        static std::set<std::pair<ID3D11Texture2D*, uint32_t>> seen;
        if (seen.size() < 200 && seen.insert({tex, spec}).second)
            log("Schatten: Textur %p mit spec %08X\n", static_cast<void*>(tex), spec);
    }
    tex->Release();
    if (!shadow) return false;
    c->PSGetShader(&g_saved_ps, nullptr, nullptr);
    c->PSGetConstantBuffers(13, 1, &g_saved_cb);
    c->PSSetShader(g_ps, nullptr, 0);
    c->PSSetConstantBuffers(13, 1, &g_cb);
    return true;
}

void shadowfix_forget(ID3D11Texture2D* tex) { g_is_shadow.erase(tex); }

void shadowfix_after(ID3D11DeviceContext* c) {
    c->PSSetShader(g_saved_ps, nullptr, 0);
    c->PSSetConstantBuffers(13, 1, &g_saved_cb);
    if (g_saved_ps) g_saved_ps->Release(), g_saved_ps = nullptr;
    if (g_saved_cb) g_saved_cb->Release(), g_saved_cb = nullptr;
}

}  // namespace host
