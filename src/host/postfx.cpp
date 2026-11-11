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

// Supersampling: Rechteck der Quelle gefiltert auf das ganze Ziel. Je Achse
// verkleinern mit Flächenfilter (Anteil jedes Quellpixels am Fußabdruck des
// Zielpixels; ganzzahliges Verhältnis: Mittel aus k×k, Verhältnis 1: 1:1),
// vergrößern mit Catmull-Rom (scharf). Danach wie ps (Bildanpassung, HDR),
// wenn adjust gesetzt ist.
cbuffer Resample : register(b1) {
    float4 src_rect;  // x0, y0, x1, y1 in Pixeln der Quelle
    float2 dst_size; float adjust; float pad2;
};
float wcub(float t) {
    t = abs(t);
    return t < 1 ? (1.5 * t - 2.5) * t * t + 1 : (t < 2 ? ((-0.5 * t + 2.5) * t - 4) * t + 2 : 0);
}
float4 ps_rs(V i) : SV_Target {
    uint sw, sh;
    scene.GetDimensions(sw, sh);
    const float2 r = (src_rect.zw - src_rect.xy) / dst_size;
    const float2 a = src_rect.xy + floor(i.pos.xy) * r;  // Fußabdruck [a, b)
    const float2 b = a + r;
    const float2 m = (a + b) * 0.5;
    const bool2 area = r >= 1;
    const int2 j0 = area ? int2(floor(a)) : int2(floor(m - 0.5)) - 1;
    const int2 j1 = min(area ? int2(ceil(b)) - 1 : j0 + 3, j0 + 15);
    float4 sum = 0;
    float wsum = 0;
    for (int y = j0.y; y <= j1.y; ++y) {
        const float wy = area.y ? max(0, min(y + 1, b.y) - max(y, a.y)) : wcub(y + 0.5 - m.y);
        for (int x = j0.x; x <= j1.x; ++x) {
            const float wx = area.x ? max(0, min(x + 1, b.x) - max(x, a.x)) : wcub(x + 0.5 - m.x);
            const float w = wx * wy;
            sum += w * scene.Load(int3(clamp(x, 0, int(sw) - 1), clamp(y, 0, int(sh) - 1), 0));
            wsum += w;
        }
    }
    float4 c = saturate(sum / wsum);
    if (adjust == 0) return c;
    float3 x = pow(saturate(c.rgb), inv_gamma);
    x = (x - 0.5) * contrast + 0.5 + brightness;
    float l = dot(x, float3(0.2126, 0.7152, 0.0722));
    x = lerp(float3(l, l, l), x, saturation);
    x = saturate(x);
    if (hdr_scale > 0) {
        float3 lin = x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
        return float4(lin * hdr_scale, 1);
    }
    return float4(x, c.a);
}
)";

ID3D11Device* g_dev = nullptr;
ID3D11VertexShader* g_vs = nullptr;
ID3D11PixelShader* g_ps = nullptr;
ID3D11PixelShader* g_ps_rs = nullptr;  // Supersampling: gefiltert
ID3D11Buffer* g_cb = nullptr;
ID3D11Buffer* g_cb_rs = nullptr;  // Rechteck und Zielgröße für ps_rs
ID3D11SamplerState* g_sampler = nullptr;
ID3D11RasterizerState* g_rs = nullptr;
ID3D11ShaderResourceView* g_scene_srv = nullptr;
UINT g_w = 0, g_h = 0;
bool g_on = false;
bool g_scaled = false;  // Szene in anderer Größe als der Bildpuffer (Supersampling)

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

// Zustände, die ein Vollbild-Dreieck anfasst: sichern und wiederherstellen
// (TS2 kann gesetzte Zustände zwischenspeichern)
struct SavedState {
    ID3D11RenderTargetView* rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11BlendState* blend = nullptr;
    float factor[4] = {};
    UINT mask = 0;
    ID3D11DepthStencilState* ds = nullptr;
    UINT ref = 0;
    ID3D11RasterizerState* rs = nullptr;
    UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    ID3D11InputLayout* il = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topo{};
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11SamplerState* smp = nullptr;
    ID3D11Buffer* cb[2] = {};

    explicit SavedState(ID3D11DeviceContext* ctx) {
        ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, &dsv);
        ctx->OMGetBlendState(&blend, factor, &mask);
        ctx->OMGetDepthStencilState(&ds, &ref);
        ctx->RSGetState(&rs);
        ctx->RSGetViewports(&nvp, vp);
        ctx->IAGetInputLayout(&il);
        ctx->IAGetPrimitiveTopology(&topo);
        ID3D11ClassInstance* ci[256];
        UINT nci = 256;
        ctx->VSGetShader(&vs, ci, &nci);
        for (UINT i = 0; i < nci; ++i) if (ci[i]) ci[i]->Release();
        nci = 256;
        ctx->PSGetShader(&ps, ci, &nci);
        for (UINT i = 0; i < nci; ++i) if (ci[i]) ci[i]->Release();
        ctx->PSGetShaderResources(0, 1, &srv);
        ctx->PSGetSamplers(0, 1, &smp);
        ctx->PSGetConstantBuffers(0, 2, cb);
    }
    void restore(ID3D11DeviceContext* ctx) {
        ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, dsv);
        ctx->OMSetBlendState(blend, factor, mask);
        ctx->OMSetDepthStencilState(ds, ref);
        ctx->RSSetState(rs);
        ctx->RSSetViewports(nvp, vp);
        ctx->IASetInputLayout(il);
        ctx->IASetPrimitiveTopology(topo);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->PSSetSamplers(0, 1, &smp);
        ctx->PSSetConstantBuffers(0, 2, cb);
        for (auto* r : rtv) if (r) r->Release();
        for (IUnknown* u : std::initializer_list<IUnknown*>{dsv, blend, ds, rs, il, vs, ps, srv, smp, cb[0], cb[1]})
            if (u) u->Release();
    }
};

// Vollbild-Dreieck mit ps_rs: Rechteck der Quelle gefiltert auf das ganze Ziel
void draw_resampled(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* src, const float rect[4],
                    ID3D11RenderTargetView* dst, UINT dst_w, UINT dst_h, bool adjust) {
    const float data[8] = {rect[0], rect[1], rect[2], rect[3], float(dst_w), float(dst_h), adjust ? 1.0f : 0.0f, 0};
    ctx->UpdateSubresource(g_cb_rs, 0, nullptr, data, 0, 0);
    ctx->OMSetRenderTargets(1, &dst, nullptr);
    ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->RSSetState(g_rs);
    D3D11_VIEWPORT vp{0, 0, float(dst_w), float(dst_h), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g_vs, nullptr, 0);
    ctx->PSSetShader(g_ps_rs, nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &src);
    ID3D11Buffer* cbs[2] = {g_cb, g_cb_rs};
    ctx->PSSetConstantBuffers(0, 2, cbs);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx->PSSetShaderResources(0, 1, &none);  // die Quelle ist danach womöglich wieder Ziel
}

}  // namespace

bool postfx_init(ID3D11Device* dev, ID3D11Texture2D* backbuffer, const PostFxSettings& s,
                 ID3D11RenderTargetView** scene_rtv, ID3D11Texture2D** scene_tex, UINT scene_w, UINT scene_h) {
    D3D11_TEXTURE2D_DESC td{};
    backbuffer->GetDesc(&td);
    const bool scaled = scene_w && scene_h && (scene_w != td.Width || scene_h != td.Height);
    if (s.neutral() && !scaled) return false;
    g_dev = dev;
    if (scaled) td.Width = scene_w, td.Height = scene_h;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // TS2 kopiert daraus in eigene Texturen dieses Formats
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = 0;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.CPUAccessFlags = 0;
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3DBlob* vsb = compile("vs", "vs_5_0");
    ID3DBlob* psb = compile("ps", "ps_5_0");
    ID3DBlob* rsb = scaled ? compile("ps_rs", "ps_5_0") : nullptr;
    bool ok = vsb && psb && (rsb || !scaled) && SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &tex)) &&
              SUCCEEDED(dev->CreateRenderTargetView(tex, nullptr, &rtv)) &&
              SUCCEEDED(dev->CreateShaderResourceView(tex, nullptr, &g_scene_srv)) &&
              SUCCEEDED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_vs)) &&
              SUCCEEDED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_ps)) &&
              (!rsb || SUCCEEDED(dev->CreatePixelShader(rsb->GetBufferPointer(), rsb->GetBufferSize(), nullptr, &g_ps_rs)));
    if (vsb) vsb->Release();
    if (psb) psb->Release();
    if (rsb) rsb->Release();
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
        D3D11_BUFFER_DESC rbd{32, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
        ok = SUCCEEDED(dev->CreateBuffer(&bd, &sd, &g_cb)) && SUCCEEDED(dev->CreateSamplerState(&smp, &g_sampler)) &&
             SUCCEEDED(dev->CreateRasterizerState(&rd, &g_rs)) &&
             (!scaled || SUCCEEDED(dev->CreateBuffer(&rbd, nullptr, &g_cb_rs)));
    }
    if (!ok) {
        log("Bildanpassung: nicht verfügbar, Bild unverändert\n");
        if (rtv) rtv->Release();
        if (tex) tex->Release();
        return false;
    }
    g_w = td.Width;
    g_h = td.Height;
    g_scaled = scaled;
    *scene_rtv = rtv;
    *scene_tex = tex;
    g_on = true;
    log("Bildanpassung: Gamma %.2f, Kontrast %.2f, Helligkeit %.2f, Sättigung %.2f, HDR-Weiß %.0f Nits\n", s.gamma,
        s.contrast, s.brightness, s.saturation, s.hdr_scale * 80.0f);
    if (scaled) log("Bildanpassung: Szene %ux%u, gefiltert auf den Bildpuffer\n", g_w, g_h);
    return true;
}

void postfx_apply(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* backbuffer_rtv, UINT out_w, UINT out_h) {
    if (!g_on) return;
    SavedState saved(ctx);
    if (g_scaled) {
        // Supersampling: ganze Szene gefiltert auf den Bildpuffer
        const float rect[4] = {0, 0, float(g_w), float(g_h)};
        draw_resampled(ctx, g_scene_srv, rect, backbuffer_rtv, out_w ? out_w : g_w, out_h ? out_h : g_h, true);
        saved.restore(ctx);
        return;
    }
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
    saved.restore(ctx);
}

bool postfx_resample(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* src, UINT src_w, UINT src_h,
                     const float rect[4], ID3D11RenderTargetView* dst, UINT dst_w, UINT dst_h) {
    if (!g_ps_rs || !src || !dst || !dst_w || !dst_h || !src_w || !src_h) return false;
    SavedState saved(ctx);
    draw_resampled(ctx, src, rect, dst, dst_w, dst_h, false);
    saved.restore(ctx);
    return true;
}

}  // namespace host
