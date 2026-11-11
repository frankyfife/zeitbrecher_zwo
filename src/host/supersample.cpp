#include "supersample.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <tuple>

#include "log.h"
#include "postfx.h"

namespace host {

namespace {

bool g_on = false;
float g_k = 1.0f;
UINT g_w = 0, g_h = 0;    // Render-Auflösung (TS2)
UINT g_pw = 0, g_ph = 0;  // k-fach
float g_sx = 1.0f, g_sy = 1.0f;  // g_pw / g_w, g_ph / g_h
std::set<ID3D11Resource*> g_scaled;

// Zustand der Kontext-Hülle: TS2s Ansichten und Scheren (Render-Auflösung)
// und ob das gebundene Ziel k-fach ist
D3D11_VIEWPORT g_vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
UINT g_nvp = 0;
D3D11_RECT g_sc[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
UINT g_nsc = 0;
bool g_target_scaled = false;

LONG sx(LONG v) { return LONG(std::lround(double(v) * g_sx)); }
LONG sy(LONG v) { return LONG(std::lround(double(v) * g_sy)); }

void apply_viewports(ID3D11DeviceContext* real) {
    D3D11_VIEWPORT v[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    const float fx = g_target_scaled ? g_sx : 1.0f, fy = g_target_scaled ? g_sy : 1.0f;
    for (UINT i = 0; i < g_nvp; ++i) {
        v[i] = g_vp[i];
        v[i].TopLeftX *= fx;
        v[i].TopLeftY *= fy;
        v[i].Width *= fx;
        v[i].Height *= fy;
    }
    real->RSSetViewports(g_nvp, v);
}

void apply_scissors(ID3D11DeviceContext* real) {
    D3D11_RECT r[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    for (UINT i = 0; i < g_nsc; ++i) r[i] = g_target_scaled ? ss_rect(g_sc[i]) : g_sc[i];
    real->RSSetScissorRects(g_nsc, r);
}

ID3D11Device* device_of(ID3D11DeviceContext* c) {
    ID3D11Device* d = nullptr;
    c->GetDevice(&d);
    if (d) d->Release();  // das Gerät lebt so lange wie der Kontext
    return d;
}

// Lesesicht einer Textur (zwischengespeichert; hält die Textur am Leben)
std::map<ID3D11Resource*, ID3D11ShaderResourceView*> g_srv;
ID3D11ShaderResourceView* srv_of(ID3D11DeviceContext* c, ID3D11Resource* r) {
    auto it = g_srv.find(r);
    if (it != g_srv.end()) return it->second;
    ID3D11ShaderResourceView* v = nullptr;
    if (FAILED(device_of(c)->CreateShaderResourceView(r, nullptr, &v))) v = nullptr;
    g_srv[r] = v;
    return v;
}

// Zwischenziel für verkleinerte Ausschnitte (je Größe und Format)
struct Temp {
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
};
std::map<std::tuple<UINT, UINT, DXGI_FORMAT>, Temp> g_temp;
Temp temp_of(ID3D11DeviceContext* c, UINT w, UINT h, DXGI_FORMAT f) {
    auto key = std::make_tuple(w, h, f);
    auto it = g_temp.find(key);
    if (it != g_temp.end()) return it->second;
    Temp t;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = f;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ID3D11Device* dev = device_of(c);
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &t.tex)) || FAILED(dev->CreateRenderTargetView(t.tex, nullptr, &t.rtv))) {
        if (t.tex) t.tex->Release();
        t = Temp{};
    }
    g_temp[key] = t;
    return t;
}

bool tex_desc(ID3D11Resource* r, D3D11_TEXTURE2D_DESC* d) {
    D3D11_RESOURCE_DIMENSION dim;
    r->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return false;
    static_cast<ID3D11Texture2D*>(r)->GetDesc(d);
    return true;
}

void warn_once(const char* what) {
    static std::set<const char*> said;
    if (said.insert(what).second) log("[supersampling] %s\n", what);
}

// Ausschnitt box (Render-Auflösung) der k-fachen Quelle gefiltert in die
// einfache Zieltextur bei (dx, dy) (Bildschirme mit Text, Scanner)
bool copy_down(ID3D11DeviceContext* real, ID3D11Resource* dst, UINT dsub, UINT dx, UINT dy, UINT dz,
               ID3D11Resource* src, const D3D11_BOX* box) {
    D3D11_TEXTURE2D_DESC sd{}, dd{};
    if (!tex_desc(src, &sd) || !tex_desc(dst, &dd)) return false;
    const UINT mip = dsub % (dd.MipLevels ? dd.MipLevels : 1);
    const UINT dw = std::max(1u, dd.Width >> mip), dh = std::max(1u, dd.Height >> mip);
    D3D11_BOX b = box ? *box : D3D11_BOX{0, 0, 0, g_w, g_h, 1};
    b.right = std::min(b.right, b.left + (dw > dx ? dw - dx : 0));
    b.bottom = std::min(b.bottom, b.top + (dh > dy ? dh - dy : 0));
    if (b.right <= b.left || b.bottom <= b.top) return true;  // nichts zu kopieren
    const UINT bw = b.right - b.left, bh = b.bottom - b.top;
    ID3D11ShaderResourceView* srv = srv_of(real, src);
    const Temp t = temp_of(real, bw, bh, dd.Format);
    const float rect[4] = {float(b.left) * g_sx, float(b.top) * g_sy, float(b.right) * g_sx, float(b.bottom) * g_sy};
    if (!srv || !t.rtv || !postfx_resample(real, srv, sd.Width, sd.Height, rect, t.rtv, bw, bh)) {
        warn_once("Kopie in kleine Textur nicht filterbar, ungefiltert");
        return false;
    }
    const D3D11_BOX all{0, 0, 0, bw, bh, 1};
    real->CopySubresourceRegion(dst, dsub, dx, dy, dz, t.tex, 0, &all);
    return true;
}

// ---- Linien
// D3D11 rastert Linien (LINELIST/LINESTRIP) ein Pixel breit, im k-fachen Bild
// also 1/k so breit und nach dem Verkleinern blasser (Scanner in Siberia,
// Handkarte). Ein Geometrie-Shader macht daraus Vierecke von k Pixeln Breite,
// wie eine Linie in der Render-Auflösung. Ein- und Ausgänge wie TS2s Shader
// (ts.vp/ts.pp: Position, Normale, Texturkoordinate, Farbe).
const char kLineGs[] = R"(
cbuffer Line : register(b0) { float2 px; float width; float pad; };
struct V { float4 pos : SV_POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; float4 col : COLOR0; };
[maxvertexcount(4)]
void gs(line V v[2], inout TriangleStream<V> s) {
    const float2 a = v[0].pos.xy / v[0].pos.w, b = v[1].pos.xy / v[1].pos.w;
    const float2 d = (b - a) / px;  // in Pixeln
    const float l = length(d);
    const float2 n = l > 1e-6 ? float2(-d.y, d.x) / l : float2(0, 1);
    const float2 o = n * px * (width * 0.5);
    V q = v[0]; q.pos.xy += o * v[0].pos.w; s.Append(q);
    q = v[0]; q.pos.xy -= o * v[0].pos.w; s.Append(q);
    q = v[1]; q.pos.xy += o * v[1].pos.w; s.Append(q);
    q = v[1]; q.pos.xy -= o * v[1].pos.w; s.Append(q);
}
)";
ID3D11GeometryShader* g_line_gs = nullptr;
ID3D11Buffer* g_line_cb = nullptr;
bool g_line_tried = false;

bool line_init(ID3D11DeviceContext* c) {
    if (g_line_tried) return g_line_gs != nullptr;
    g_line_tried = true;
    ID3DBlob *code = nullptr, *err = nullptr;
    ID3D11Device* dev = device_of(c);
    D3D11_BUFFER_DESC bd{16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    if (FAILED(D3DCompile(kLineGs, sizeof kLineGs - 1, "ss_lines", nullptr, nullptr, "gs", "gs_5_0", 0, 0, &code, &err)) ||
        FAILED(dev->CreateGeometryShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_line_gs)) ||
        FAILED(dev->CreateBuffer(&bd, nullptr, &g_line_cb))) {
        log("[supersampling] Linien-Shader nicht verfügbar: %s\n", err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
        if (g_line_gs) g_line_gs->Release(), g_line_gs = nullptr;
    }
    if (code) code->Release();
    if (err) err->Release();
    return g_line_gs != nullptr;
}

// ---- Probe (TS2_SSPROBE=1)
bool g_probe = false;
long g_topo[16] = {};
int g_frames = 0;

}  // namespace

void ss_init(float k, UINT w, UINT h) {
    g_w = w;
    g_h = h;
    if (!(k >= 1.0f)) k = 1.0f;
    k = std::min(k, 4.0f);
    const UINT side = std::max(w, h);
    if (side && k * float(side) > float(D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION))
        k = float(D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) / float(side);
    g_k = k;
    g_pw = UINT(std::lround(double(w) * k));
    g_ph = UINT(std::lround(double(h) * k));
    g_on = g_pw != w || g_ph != h;
    g_sx = w ? float(g_pw) / float(w) : 1.0f;
    g_sy = h ? float(g_ph) / float(h) : 1.0f;
    g_probe = GetEnvironmentVariableW(L"TS2_SSPROBE", nullptr, 0) > 0;
    if (g_on) log("Supersampling: %.2fx, TS2 rechnet in %ux%u, gezeichnet in %ux%u\n", g_k, w, h, g_pw, g_ph);
    else log("Supersampling: aus\n");
}

bool ss_on() { return g_on; }
float ss_factor() { return g_on ? g_k : 1.0f; }
UINT ss_width() { return g_pw; }
UINT ss_height() { return g_ph; }

bool ss_scale_desc(D3D11_TEXTURE2D_DESC* d) {
    if (!g_on || !d || d->Width != g_w || d->Height != g_h || d->ArraySize != 1 || d->SampleDesc.Count != 1 ||
        d->Usage != D3D11_USAGE_DEFAULT || d->CPUAccessFlags)
        return false;
    // Tiefe (0x14122B78C) und Bildkopien (nur Lesesicht, eine Stufe; geladene
    // Texturen legt TS2 mit Render-Ziel und Mipmap-Erzeugung an, 0x14122B438)
    const bool depth = (d->BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0;
    const bool copy = d->BindFlags == D3D11_BIND_SHADER_RESOURCE && d->MiscFlags == 0 && d->MipLevels == 1;
    if (!depth && !copy) return false;
    d->Width = g_pw;
    d->Height = g_ph;
    return true;
}

void ss_register(ID3D11Resource* r) {
    if (r) g_scaled.insert(r);
}
void ss_forget(ID3D11Resource* r) { g_scaled.erase(r); }
bool ss_is_scaled(ID3D11Resource* r) { return g_on && r && g_scaled.count(r) != 0; }

D3D11_RECT ss_rect(const D3D11_RECT& r) { return D3D11_RECT{sx(r.left), sy(r.top), sx(r.right), sy(r.bottom)}; }

void ss_set_targets(ID3D11DeviceContext* real, ID3D11RenderTargetView* rtv0, ID3D11DepthStencilView* dsv) {
    ID3D11Resource* res = nullptr;
    if (rtv0) rtv0->GetResource(&res);
    else if (dsv) dsv->GetResource(&res);
    const bool scaled = ss_is_scaled(res);
    if (res) res->Release();
    if (scaled == g_target_scaled) return;
    g_target_scaled = scaled;
    if (g_nvp) apply_viewports(real);
    if (g_nsc) apply_scissors(real);
}

void ss_set_viewports(ID3D11DeviceContext* real, UINT n, const D3D11_VIEWPORT* v) {
    g_nvp = std::min<UINT>(n, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    for (UINT i = 0; i < g_nvp; ++i) g_vp[i] = v[i];
    apply_viewports(real);
}

void ss_get_viewports(UINT* n, D3D11_VIEWPORT* v) {
    // wie D3D11: ohne Feld die Anzahl, sonst bis *n Einträge (Rest genullt)
    if (!n) return;
    if (!v) {
        *n = g_nvp;
        return;
    }
    for (UINT i = 0; i < *n; ++i) v[i] = i < g_nvp ? g_vp[i] : D3D11_VIEWPORT{};
}

void ss_set_scissors(ID3D11DeviceContext* real, UINT n, const D3D11_RECT* r) {
    g_nsc = std::min<UINT>(n, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    for (UINT i = 0; i < g_nsc; ++i) g_sc[i] = r[i];
    apply_scissors(real);
}

void ss_get_scissors(UINT* n, D3D11_RECT* r) {
    if (!n) return;
    if (!r) {
        *n = g_nsc;
        return;
    }
    for (UINT i = 0; i < *n; ++i) r[i] = i < g_nsc ? g_sc[i] : D3D11_RECT{};
}

void ss_clear_state() {
    g_nvp = g_nsc = 0;
    g_target_scaled = false;
}

bool ss_copy_region(ID3D11DeviceContext* real, ID3D11Resource* dst, UINT dsub, UINT dx, UINT dy, UINT dz,
                    ID3D11Resource* src, UINT ssub, const D3D11_BOX* box) {
    const bool sd = ss_is_scaled(dst), ss = ss_is_scaled(src);
    if (!sd && !ss) return false;
    if (sd && ss) {
        // TS2s Bildkopien (0x14122B384): Rechteck k-fach
        if (!box) {
            real->CopySubresourceRegion(dst, dsub, UINT(sx(LONG(dx))), UINT(sy(LONG(dy))), dz, src, ssub, nullptr);
            return true;
        }
        D3D11_BOX b = *box;
        b.left = UINT(sx(LONG(b.left)));
        b.right = std::min<UINT>(UINT(sx(LONG(b.right))), g_pw);
        b.top = UINT(sy(LONG(b.top)));
        b.bottom = std::min<UINT>(UINT(sy(LONG(b.bottom))), g_ph);
        if (b.right > b.left && b.bottom > b.top)
            real->CopySubresourceRegion(dst, dsub, UINT(sx(LONG(dx))), UINT(sy(LONG(dy))), dz, src, ssub, &b);
        return true;
    }
    if (ss) return ssub == 0 && copy_down(real, dst, dsub, dx, dy, dz, src, box);
    // in eine k-fache Textur aus einer einfachen: kommt in TS2 nicht vor
    warn_once("Kopie aus einfacher in k-fache Textur (nur Lage umgerechnet)");
    real->CopySubresourceRegion(dst, dsub, UINT(sx(LONG(dx))), UINT(sy(LONG(dy))), dz, src, ssub, box);
    return true;
}

bool ss_copy_resource(ID3D11DeviceContext* real, ID3D11Resource* dst, ID3D11Resource* src) {
    const bool sd = ss_is_scaled(dst), ss = ss_is_scaled(src);
    if (sd == ss) return false;
    warn_once("CopyResource zwischen einfacher und k-facher Textur");
    return ss_copy_region(real, dst, 0, 0, 0, 0, src, 0, nullptr);
}

void ss_update_subresource(ID3D11Resource* dst) {
    if (ss_is_scaled(dst)) warn_once("UpdateSubresource in eine k-fache Textur (nicht umgerechnet)");
}

void ss_get_data(ID3D11Asynchronous* a, void* data, UINT size) {
    if (!data || size < 4 || !a) return;
    ID3D11Query* q = nullptr;
    if (FAILED(a->QueryInterface(__uuidof(ID3D11Query), reinterpret_cast<void**>(&q))) || !q) return;
    D3D11_QUERY_DESC d{};
    q->GetDesc(&d);
    q->Release();
    if (d.Query != D3D11_QUERY_OCCLUSION) return;
    const double f = double(g_sx) * double(g_sy);
    auto scale = [f](uint64_t v) -> uint64_t { return v ? std::max<uint64_t>(1, uint64_t(std::llround(double(v) / f))) : 0; };
    if (size >= 8) *static_cast<uint64_t*>(data) = scale(*static_cast<uint64_t*>(data));
    else *static_cast<uint32_t*>(data) = uint32_t(scale(*static_cast<uint32_t*>(data)));
}

ID3D11Texture2D* ss_shot(ID3D11DeviceContext* ctx, ID3D11Texture2D* scene) {
    if (!g_on || !scene) return nullptr;
    const Temp t = temp_of(ctx, g_w, g_h, DXGI_FORMAT_R8G8B8A8_UNORM);
    ID3D11ShaderResourceView* srv = srv_of(ctx, scene);
    const float rect[4] = {0, 0, float(g_pw), float(g_ph)};
    if (!t.rtv || !srv || !postfx_resample(ctx, srv, g_pw, g_ph, rect, t.rtv, g_w, g_h)) return nullptr;
    return t.tex;
}

bool ss_line_before(ID3D11DeviceContext* real) {
    if (!g_on || !g_target_scaled) return false;
    D3D11_PRIMITIVE_TOPOLOGY t{};
    real->IAGetPrimitiveTopology(&t);
    if (t != D3D11_PRIMITIVE_TOPOLOGY_LINELIST && t != D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP) return false;
    ID3D11GeometryShader* bound = nullptr;
    real->GSGetShader(&bound, nullptr, nullptr);
    if (bound) {  // TS2 setzt keinen; ein fremder bleibt unangetastet
        bound->Release();
        return false;
    }
    if (!line_init(real)) return false;
    UINT n = 1;
    D3D11_VIEWPORT vp{};
    real->RSGetViewports(&n, &vp);
    if (!n || vp.Width <= 0 || vp.Height <= 0) return false;
    const float data[4] = {2.0f / vp.Width, 2.0f / vp.Height, 0.5f * (g_sx + g_sy), 0};
    real->UpdateSubresource(g_line_cb, 0, nullptr, data, 0, 0);
    real->GSSetShader(g_line_gs, nullptr, 0);
    real->GSSetConstantBuffers(0, 1, &g_line_cb);
    return true;
}

void ss_line_after(ID3D11DeviceContext* real) {
    ID3D11Buffer* none = nullptr;
    real->GSSetShader(nullptr, nullptr, 0);
    real->GSSetConstantBuffers(0, 1, &none);
}

bool ss_probe_on() { return g_probe; }

void ss_probe_draw(ID3D11DeviceContext* real) {
    D3D11_PRIMITIVE_TOPOLOGY t{};
    real->IAGetPrimitiveTopology(&t);
    ++g_topo[unsigned(t) < 16 ? unsigned(t) : 0];
}

void ss_probe_frame_end() {
    if (!g_probe || ++g_frames < 600) return;
    log("[supersampling] Zeichnungen je Grundform in 600 Bildern (k=%.2f): Punkte %ld, Linien %ld, Linienzug %ld, "
        "Dreiecke %ld, Streifen %ld\n", ss_factor(), g_topo[1], g_topo[2], g_topo[3], g_topo[4], g_topo[5]);
    g_frames = 0;
    for (long& c : g_topo) c = 0;
}

}  // namespace host
