#include "shadowfix.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>
#include <map>

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
    return float4(t.rgb, t.a * strength);
}
)";

ID3D11Device* g_dev = nullptr;
ID3D11PixelShader* g_ps = nullptr;
ID3D11Buffer* g_cb = nullptr;
std::map<ID3D11Texture2D*, bool> g_is_shadow;  // je Textur einmal geprüft
ID3D11PixelShader* g_saved_ps = nullptr;
ID3D11Buffer* g_saved_cb = nullptr;

// Schattentextur: 16×16, Farbe überall schwarz, Alpha in der Mitte ≈ 0,62,
// am Rand 0
bool check_texture(ID3D11DeviceContext* c, ID3D11Texture2D* tex) {
    D3D11_TEXTURE2D_DESC d;
    tex->GetDesc(&d);
    if (d.Width != 16 || d.Height != 16 || d.Format != DXGI_FORMAT_R8G8B8A8_UNORM) return false;
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
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(c->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
        int rgb_max = 0, a_max = 0, a_corner = 0;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const uint8_t* p = static_cast<const uint8_t*>(m.pData) + y * m.RowPitch + x * 4;
                rgb_max = std::max({rgb_max, int(p[0]), int(p[1]), int(p[2])});
                a_max = std::max(a_max, int(p[3]));
                if ((x == 0 || x == 15) && (y == 0 || y == 15)) a_corner = std::max(a_corner, int(p[3]));
            }
        c->Unmap(st, 0);
        shadow = rgb_max == 0 && a_max >= 100 && a_max <= 200 && a_corner == 0;
    }
    st->Release();
    if (shadow) log("Schatten: Textur %p erkannt\n", static_cast<void*>(tex));
    return shadow;
}

}  // namespace

bool shadowfix_init(ID3D11Device* dev, float strength) {
    if (strength <= 0.0f) return false;
    g_dev = dev;
    ID3DBlob *code = nullptr, *err = nullptr;
    if (FAILED(D3DCompile(kShader, sizeof kShader - 1, "shadowfix", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &code, &err))) {
        log("Schatten: Shader nicht übersetzbar: %s\n", err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
        if (err) err->Release();
        return false;
    }
    if (err) err->Release();
    // Mitte der Textur hat Alpha ≈ 0,62: auf die gewünschte Deckkraft bringen
    const float data[4] = {strength / 0.62f, 0, 0, 0};
    D3D11_BUFFER_DESC bd{sizeof data, D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    D3D11_SUBRESOURCE_DATA sd{data, 0, 0};
    const bool ok = SUCCEEDED(dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_ps)) &&
                    SUCCEEDED(dev->CreateBuffer(&bd, &sd, &g_cb));
    code->Release();
    if (ok) log("Schatten: feste Deckkraft %.2f statt der Eckenfarbe\n", strength);
    return ok;
}

bool shadowfix_before(ID3D11DeviceContext* c) {
    if (!g_ps) return false;
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
