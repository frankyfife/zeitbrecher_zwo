#include "postfx.h"

#include <d3dcompiler.h>

#include <cstring>
#include <initializer_list>

#include "log.h"

#pragma comment(lib, "d3dcompiler.lib")

namespace host {

namespace {

const char kShader[] = R"(
cbuffer Adjust : register(b0) {
    float inv_gamma; float contrast; float brightness; float saturation;
    float hdr_scale; float3 pad;
};
Texture2D scene : register(t0);
SamplerState pt : register(s0);
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
V vs(uint id : SV_VertexID) {
    V o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 ps(V i) : SV_Target {
    float4 c = scene.Sample(pt, i.uv);
    float3 x = pow(saturate(c.rgb), inv_gamma);
    x = (x - 0.5) * contrast + 0.5 + brightness;
    float l = dot(x, float3(0.2126, 0.7152, 0.0722));
    x = lerp(float3(l, l, l), x, saturation);
    x = saturate(x);
    if (hdr_scale > 0) {
        // TS2 rechnet im Gamma-Raum (sRGB-nahe Texturen, UNORM-Ziel): für
        // scRGB linearisieren und auf das gewählte Weiß legen
        float3 lin = x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
        return float4(lin * hdr_scale, 1);
    }
    return float4(x, c.a);
}
)";

ID3D11Device* g_dev = nullptr;
ID3D11VertexShader* g_vs = nullptr;
ID3D11PixelShader* g_ps = nullptr;
ID3D11Buffer* g_cb = nullptr;
ID3D11SamplerState* g_sampler = nullptr;
ID3D11RasterizerState* g_rs = nullptr;
ID3D11ShaderResourceView* g_scene_srv = nullptr;
UINT g_w = 0, g_h = 0;
bool g_on = false;

ID3DBlob* compile(const char* entry, const char* target) {
    ID3DBlob *code = nullptr, *err = nullptr;
    if (FAILED(D3DCompile(kShader, sizeof kShader - 1, "postfx", nullptr, nullptr, entry, target, 0, 0, &code, &err))) {
        log("Bildanpassung: Shader %s nicht übersetzbar: %s\n", entry, err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
        if (err) err->Release();
        return nullptr;
    }
    if (err) err->Release();
    return code;
}

}  // namespace

bool postfx_init(ID3D11Device* dev, ID3D11Texture2D* backbuffer, const PostFxSettings& s,
                 ID3D11RenderTargetView** scene_rtv, ID3D11Texture2D** scene_tex) {
    if (s.neutral()) return false;
    g_dev = dev;
    D3D11_TEXTURE2D_DESC td{};
    backbuffer->GetDesc(&td);
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // TS2 kopiert daraus in eigene Texturen dieses Formats
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = 0;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.CPUAccessFlags = 0;
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3DBlob* vsb = compile("vs", "vs_5_0");
    ID3DBlob* psb = compile("ps", "ps_5_0");
    bool ok = vsb && psb && SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &tex)) &&
              SUCCEEDED(dev->CreateRenderTargetView(tex, nullptr, &rtv)) &&
              SUCCEEDED(dev->CreateShaderResourceView(tex, nullptr, &g_scene_srv)) &&
              SUCCEEDED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_vs)) &&
              SUCCEEDED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_ps));
    if (vsb) vsb->Release();
    if (psb) psb->Release();
    if (ok) {
        const float data[8] = {1.0f / (s.gamma > 0.05f ? s.gamma : 0.05f), s.contrast, s.brightness, s.saturation,
                               s.hdr_scale, 0, 0, 0};
        D3D11_BUFFER_DESC bd{sizeof data, D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
        D3D11_SUBRESOURCE_DATA sd{data, 0, 0};
        D3D11_SAMPLER_DESC smp{};
        smp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        smp.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        ok = SUCCEEDED(dev->CreateBuffer(&bd, &sd, &g_cb)) && SUCCEEDED(dev->CreateSamplerState(&smp, &g_sampler)) &&
             SUCCEEDED(dev->CreateRasterizerState(&rd, &g_rs));
    }
    if (!ok) {
        log("Bildanpassung: nicht verfügbar, Bild unverändert\n");
        if (rtv) rtv->Release();
        if (tex) tex->Release();
        return false;
    }
    g_w = td.Width;
    g_h = td.Height;
    *scene_rtv = rtv;
    *scene_tex = tex;
    g_on = true;
    log("Bildanpassung: Gamma %.2f, Kontrast %.2f, Helligkeit %.2f, Sättigung %.2f, HDR-Weiß %.0f Nits\n", s.gamma,
        s.contrast, s.brightness, s.saturation, s.hdr_scale * 80.0f);
    return true;
}

void postfx_apply(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* backbuffer_rtv) {
    if (!g_on) return;
    // Zustände sichern, die dieser Schritt anfasst
    ID3D11RenderTargetView* old_rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* old_dsv = nullptr;
    ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, old_rtv, &old_dsv);
    ID3D11BlendState* old_blend = nullptr;
    float old_factor[4];
    UINT old_mask = 0;
    ctx->OMGetBlendState(&old_blend, old_factor, &old_mask);
    ID3D11DepthStencilState* old_ds = nullptr;
    UINT old_ref = 0;
    ctx->OMGetDepthStencilState(&old_ds, &old_ref);
    ID3D11RasterizerState* old_rs = nullptr;
    ctx->RSGetState(&old_rs);
    UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT old_vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    ctx->RSGetViewports(&nvp, old_vp);
    ID3D11InputLayout* old_il = nullptr;
    ctx->IAGetInputLayout(&old_il);
    D3D11_PRIMITIVE_TOPOLOGY old_topo;
    ctx->IAGetPrimitiveTopology(&old_topo);
    ID3D11VertexShader* old_vs = nullptr;
    ID3D11PixelShader* old_ps = nullptr;
    ID3D11ClassInstance* ci[256];
    UINT nci = 256;
    ctx->VSGetShader(&old_vs, ci, &nci);
    for (UINT i = 0; i < nci; ++i) if (ci[i]) ci[i]->Release();
    nci = 256;
    ctx->PSGetShader(&old_ps, ci, &nci);
    for (UINT i = 0; i < nci; ++i) if (ci[i]) ci[i]->Release();
    ID3D11ShaderResourceView* old_srv = nullptr;
    ctx->PSGetShaderResources(0, 1, &old_srv);
    ID3D11SamplerState* old_smp = nullptr;
    ctx->PSGetSamplers(0, 1, &old_smp);
    ID3D11Buffer* old_cb = nullptr;
    ctx->PSGetConstantBuffers(0, 1, &old_cb);

    // angepasst in den Bildpuffer
    ctx->OMSetRenderTargets(1, &backbuffer_rtv, nullptr);
    ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->RSSetState(g_rs);
    D3D11_VIEWPORT vp{0, 0, float(g_w), float(g_h), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g_vs, nullptr, 0);
    ctx->PSSetShader(g_ps, nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &g_scene_srv);
    ctx->PSSetSamplers(0, 1, &g_sampler);
    ctx->PSSetConstantBuffers(0, 1, &g_cb);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx->PSSetShaderResources(0, 1, &none);  // die Szene ist im nächsten Bild wieder Ziel

    // wiederherstellen
    ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, old_rtv, old_dsv);
    ctx->OMSetBlendState(old_blend, old_factor, old_mask);
    ctx->OMSetDepthStencilState(old_ds, old_ref);
    ctx->RSSetState(old_rs);
    ctx->RSSetViewports(nvp, old_vp);
    ctx->IASetInputLayout(old_il);
    ctx->IASetPrimitiveTopology(old_topo);
    ctx->VSSetShader(old_vs, nullptr, 0);
    ctx->PSSetShader(old_ps, nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &old_srv);
    ctx->PSSetSamplers(0, 1, &old_smp);
    ctx->PSSetConstantBuffers(0, 1, &old_cb);
    for (auto* r : old_rtv) if (r) r->Release();
    for (IUnknown* u : std::initializer_list<IUnknown*>{old_dsv, old_blend, old_ds, old_rs, old_il, old_vs, old_ps, old_srv,
                                                       old_smp, old_cb})
        if (u) u->Release();
}

}  // namespace host
