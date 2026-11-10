#include "effectfix.h"

#include <windows.h>

#include <d3dcompiler.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "log.h"

namespace host {

namespace {

const Ts2Image* g_img = nullptr;
ID3D11Device* g_dev = nullptr;

// TS2s Blend-Zustände: 0x14301ECE0[Mischart·16 + Schreibmaske], angelegt
// von 0x14122A860(Art, op, quelle, ziel) beim Einrichten (0x14122B78C);
// gewählt mit 0x14122A5BC(Art), die aktuelle Art steht in 0x143020A98
constexpr uint64_t kBlendTable = 0x14301ECE0;
constexpr uint64_t kBlendMode = 0x143020A98;
constexpr int kRainDarken = 9;  // nur die Regentropfen (0x1411012BD)

ID3D11BlendState* g_rain_states[16] = {};

// Art 9: SUBTRACT -> REV_SUBTRACT (Ziel − Quelle·Ziel). Prüft bei jeder
// Zeichnung, ob die Tabelle noch die eigenen Zustände hält (TS2 legt sie
// einmal beim Start an).
void fix_rain_blend() {
    auto** table = g_img->va<ID3D11BlendState*>(kBlendTable) + kRainDarken * 16;
    for (int i = 0; i < 16; ++i) {
        if (!table[i] || table[i] == g_rain_states[i]) continue;
        D3D11_BLEND_DESC d{};
        table[i]->GetDesc(&d);
        auto& r = d.RenderTarget[0];
        if (r.BlendOp != D3D11_BLEND_OP_SUBTRACT) continue;  // schon anders: nichts tun
        r.BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
        if (r.BlendOpAlpha == D3D11_BLEND_OP_SUBTRACT) r.BlendOpAlpha = D3D11_BLEND_OP_REV_SUBTRACT;
        ID3D11BlendState* st = nullptr;
        if (FAILED(g_dev->CreateBlendState(&d, &st))) continue;
        // der alte Zustand bleibt bestehen (TS2 hält ihn womöglich gebunden)
        table[i] = st;
        g_rain_states[i] = st;
        if (i == 0) log("Regen auf der Kamera: Mischart 9 abdunkelnd (REV_SUBTRACT)\n");
    }
}

// ---- Licht (#4)
//
// TS2s einziges Shader-Paar (data/shaders/ts.vp, ts.pp) nachgebaut, gleiche
// Eingänge und Konstantenpuffer; geändert sind nur die Lichtschleifen:
//  - Pixel-Shader (spec 0x20000000, Levelgeometrie): Das Original rechnet den
//    Lichtvektor gegen SV_POSITION (Bildschirmpixel und Tiefe) und dämpft
//    mit lightpos.w/lightcol.w, die TS2 als 0 und 1 übergibt (keine
//    Dämpfung). Ein Mündungsfeuer erhellte so Flächen im ganzen Bild, je
//    nach Bildschirmlage. Die Lichter liegen hier im Modellraum (für das
//    Level die Welt, gemessen: das Mündungsfeuer 1,6 vor der Kamera); der
//    Ersatz bekommt die Modelllage vom Vertex-Shader und dämpft mit
//    falloffs (min, max, 1/(max−min)) wie TS2s Vertex-Shader.
//  - Vertex-Shader (spec 0x200, Figuren und Waffen): Die Lichter liegen im
//    Sichtraum, das Original zieht aber die schon projizierte Lage ab; der
//    Ersatz nimmt die Lage vor der Projektion.
const char kLightShaders[] = R"(
cbuffer PER_INSTANCE : register(b0) {
    float4 mvr[4]; float4 ambient; uint spec; float alphaRefMin; float alphaRefMax; float bias;
    float2 detailUVScale; float2 padding;
};
cbuffer PER_FRAME : register(b1) { float4 projr[4]; };
cbuffer MATRICES : register(b2) { float4 mat[100]; };
cbuffer LIGHTS : register(b3) { float4 lightpos[3]; float4 lightcol[3]; float4 falloffs[3]; };
Texture2D textures[2] : register(t0);
SamplerState samplers[2] : register(s0);

struct VI {
    float3 pos : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; float2 uv1 : TEXCOORD1;
    float4 col : COLOR0; uint mi : TEXCOORD2;
};
struct VO {
    float4 pos : SV_POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; float4 col : COLOR0;
    float3 mpos : TEXCOORD1;
};

float att(float d, float4 f) { return (f.y - clamp(d, f.x, f.y)) * f.z; }

VO vs(VI v) {
    VO o;
    uint f = spec;
    float4 c = (f & 1) ? v.col.wzyx : float4(0.5, 0.5, 0.5, 0.5);
    c = (f & 2) ? v.col : c;
    c *= 2.007874;
    float4 m0, m1, m2, m3;  // Spalten (x, y, z, w) wie im Original
    if (f & 320) {
        uint i = v.mi * 4;
        m0 = float4(mat[i].x, mat[i + 1].x, mat[i + 2].x, mat[i + 3].x);
        m1 = float4(mat[i].y, mat[i + 1].y, mat[i + 2].y, mat[i + 3].y);
        m2 = float4(mat[i].z, mat[i + 1].z, mat[i + 2].z, mat[i + 3].z);
        m3 = float4(mat[i].w, mat[i + 1].w, mat[i + 2].w, mat[i + 3].w);
    } else {
        m0 = float4(mvr[0].x, mvr[1].x, mvr[2].x, mvr[3].x);
        m1 = float4(mvr[0].y, mvr[1].y, mvr[2].y, mvr[3].y);
        m2 = float4(mvr[0].z, mvr[1].z, mvr[2].z, mvr[3].z);
        m3 = float4(mvr[0].w, mvr[1].w, mvr[2].w, mvr[3].w);
    }
    float4 p = float4(v.pos, 1);
    float3 q = float3(dot(m0, p), dot(m1, p), dot(m2, p));
    float4 r5;
    r5.w = dot(m3, p);
    r5.xy = (f & 0x10000) ? q.xy + v.uv1 : q.xy;
    float2 euv = float2(dot(v.uv, m0.xy), dot(v.uv, m1.xy)) * 0.5 + 0.5;
    o.uv = (f & 0x01000000) ? euv : v.uv;
    r5.z = q.z + bias;
    float3 vpos = float3(r5.xy, q.z);  // Sichtraum
    float4 clip = projr[0] * r5.x + projr[1] * r5.y + projr[2] * r5.z + projr[3] * r5.w;
    o.pos = (f & 32) ? r5 : clip;
    if (f & 1024) {
        float4 a = c * ambient;
        float3 nv = float3(dot(m0.xyz, v.n), dot(m1.xyz, v.n), dot(m2.xyz, v.n));
        float3 s = a.xyz;
        for (int k = 0; k < 3; ++k) s += max(dot(nv, -lightpos[k].xyz), 0) * lightcol[k].xyz;
        o.col = float4(s, a.w + 3.0);
    } else if (f & 512) {
        float4 a = c * ambient;
        float3 nv = float3(dot(m0.xyz, v.n), dot(m1.xyz, v.n), dot(m2.xyz, v.n));
        for (int k = 0; k < 3; ++k) {
            float3 L = lightpos[k].xyz - vpos;
            float d = length(L);
            float l = max(dot(nv, L), 0) / max(d, 1e-4) * att(d, falloffs[k]);
            a += float4(l * lightcol[k].xyz, 1);
        }
        o.col = a;
    } else {
        o.col = c;
    }
    o.n = v.n;
    o.mpos = v.pos;
    return o;
}

float4 ps(VO i) : SV_Target {
    uint f = spec;
    float4 t = float4(1, 1, 1, 1);
    if (f & 0x01000004) {
        t = textures[0].Sample(samplers[0], i.uv);
        if (f & 8192) {
            float2 duv = i.uv * detailUVScale;
            float3 d1 = textures[1].Sample(samplers[1], duv).xyz;
            float3 d2 = textures[1].Sample(samplers[1], duv * 8).xyz;
            t.xyz = t.xyz + d1 - 0.5 + d2 - 0.5;
        }
    }
    float4 r;
    if (f & 0x20000000) {
        float3 s = 0;
        float3 n = normalize(i.n);
        for (int k = 0; k < 3; ++k) {
            float3 L = lightpos[k].xyz - i.mpos;
            float d = length(L);
            s += max(dot(L, n), 0) / max(d, 1e-4) * att(d, falloffs[k]) * lightcol[k].xyz;
        }
        r = t * (float4(s * 1.5, 0) + i.col);
    } else {
        r = t * i.col;
    }
    if (r.w < alphaRefMin || alphaRefMax < r.w) discard;
    return r;
}
)";

ID3D11VertexShader* g_light_vs = nullptr;
ID3D11PixelShader* g_light_ps = nullptr;
ID3D11VertexShader* g_saved_vs = nullptr;
ID3D11PixelShader* g_saved_ps = nullptr;
bool g_light_ps_on = true, g_light_vs_on = true;
constexpr uint64_t kCurSpec = 0x142FE3828;  // spec der zuletzt gesetzten Instanz (0x141229434)

bool compile(const char* entry, const char* target, ID3DBlob** out) {
    ID3DBlob* err = nullptr;
    if (FAILED(D3DCompile(kLightShaders, sizeof kLightShaders - 1, "effectfix", nullptr, nullptr, entry, target,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err))) {
        log("Licht: Shader %s nicht übersetzbar: %s\n", entry,
            err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
        if (err) err->Release();
        return false;
    }
    if (err) err->Release();
    return true;
}

void light_init() {
    wchar_t v[8] = {};
    if (GetEnvironmentVariableW(L"TS2_LICHTFIX", v, 8) > 0) {
        // 0 aus, 1 nur Pixel-Shader (Level), 2 nur Vertex-Shader (Figuren)
        g_light_ps_on = v[0] == L'1';
        g_light_vs_on = v[0] == L'2';
    }
    if (!g_light_ps_on && !g_light_vs_on) return;
    ID3DBlob *vsb = nullptr, *psb = nullptr;
    if (compile("vs", "vs_5_0", &vsb) && compile("ps", "ps_5_0", &psb) &&
        SUCCEEDED(g_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_light_vs)) &&
        SUCCEEDED(g_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_light_ps)))
        log("Licht: eigene Lichtschleifen (Level %s, Figuren %s)\n", g_light_ps_on ? "an" : "aus",
            g_light_vs_on ? "an" : "aus");
    if (vsb) vsb->Release();
    if (psb) psb->Release();
}

// vor einer Zeichnung: eigene Shader, wenn TS2s Lichtschleife liefe
bool light_before(ID3D11DeviceContext* c) {
    if (!g_light_vs || !g_light_ps) return false;
    const uint32_t f = *g_img->va<uint32_t>(kCurSpec);
    const bool ps_light = g_light_ps_on && (f & 0x20000000u);
    const bool vs_light = g_light_vs_on && (f & 0x200u) && !(f & 0x400u);
    if (!ps_light && !vs_light) return false;
    c->VSGetShader(&g_saved_vs, nullptr, nullptr);
    c->PSGetShader(&g_saved_ps, nullptr, nullptr);
    c->VSSetShader(g_light_vs, nullptr, 0);
    if (ps_light) c->PSSetShader(g_light_ps, nullptr, 0);
    return true;
}

void light_after(ID3D11DeviceContext* c) {
    c->VSSetShader(g_saved_vs, nullptr, 0);
    c->PSSetShader(g_saved_ps, nullptr, 0);
    if (g_saved_vs) g_saved_vs->Release(), g_saved_vs = nullptr;
    if (g_saved_ps) g_saved_ps->Release(), g_saved_ps = nullptr;
}

bool g_light_drawn = false;

}  // namespace

bool effectfix_install(const Ts2Image& img, ID3D11Device* dev) {
    g_img = &img;
    g_dev = dev;
    wchar_t v[8] = {};
    if (GetEnvironmentVariableW(L"TS2_EFFEKTFIX", v, 8) > 0 && v[0] == L'0') {
        log("Effekte: Korrekturen aus (TS2_EFFEKTFIX=0)\n");
        return false;
    }
    light_init();
    return true;
}

// TS2_KOPIEPROBE=1: Bildschirmkopien (0x14122B384: kopiert die linke obere
// Ecke des Bildpuffers in eine Textur) – Größen bei jeder Änderung melden
static void copy_probe() {
    static const bool on = GetEnvironmentVariableW(L"TS2_KOPIEPROBE", nullptr, 0) > 0;
    if (!on) return;
    struct Snap {
        uint32_t v[12];
    } s{};
    auto ent = [&](uint64_t e, int k) {
        s.v[k] = *g_img->va<uint16_t>(e + 0x24);
        s.v[k + 1] = *g_img->va<uint16_t>(e + 0x28);
        s.v[k + 2] = uint32_t(*g_img->va<int32_t>(e + 0x34));
    };
    ent(0x142FFD9A8, 0);
    ent(0x142FE5968, 3);
    s.v[6] = *g_img->va<uint32_t>(0x142FFD9A4);
    s.v[7] = *g_img->va<uint32_t>(0x14301F9F4);
    const uint64_t cur = *g_img->va<uint64_t>(0x142EE6510);
    s.v[8] = uint32_t(cur);
    static Snap last{};
    if (memcmp(&s, &last, sizeof s) == 0) return;
    last = s;
    log("[kopie] vorher %ux%u Tex %d, jetzt %ux%u Tex %d, Grenze %ux%u, Quelle %llX\n", s.v[0], s.v[1], int(s.v[2]),
        s.v[3], s.v[4], int(s.v[5]), s.v[6], s.v[7], static_cast<unsigned long long>(cur));
}

bool effectfix_before(ID3D11DeviceContext* c) {
    fix_rain_blend();
    copy_probe();
    // TS2_SICHTART=<n>: Sichtart des Spielers erzwingen (0x143F28880 +
    // Spieler·0x12D8; 7 = Kamera-Sucher 0x141139844), nur Probe
    static const int force_view = [] {
        wchar_t v[16] = {};
        return GetEnvironmentVariableW(L"TS2_SICHTART", v, 16) > 0 ? _wtoi(v) : -1;
    }();
    if (force_view >= 0)
        if (uint8_t* p = *g_img->va<uint8_t*>(0x143F1A388))
            *g_img->va<int32_t>(0x143F28880 + uint64_t(*reinterpret_cast<int32_t*>(p + 4)) * 0x12D8) = force_view;
    g_light_drawn = light_before(c);
    return g_light_drawn;
}

void effectfix_after(ID3D11DeviceContext* c) {
    if (g_light_drawn) light_after(c), g_light_drawn = false;
}

int effectfix_blend_mode() { return g_img ? *g_img->va<int>(kBlendMode) : -1; }

void effectfix_log_blend_modes() {
    if (!g_img) return;
    auto** table = g_img->va<ID3D11BlendState*>(kBlendTable);
    for (int m = 0; m < 24; ++m) {
        ID3D11BlendState* st = table[m * 16 + 15];
        if (!st) continue;
        D3D11_BLEND_DESC d{};
        st->GetDesc(&d);
        const auto& r = d.RenderTarget[0];
        log("[mischart] %d: an %d, Farbe %d*%d op %d, Alpha %d*%d op %d\n", m, int(r.BlendEnable), int(r.SrcBlend),
            int(r.DestBlend), int(r.BlendOp), int(r.SrcBlendAlpha), int(r.DestBlendAlpha), int(r.BlendOpAlpha));
    }
}

namespace {
// Konstantenpuffer lesen (Kopie in einen Staging-Puffer)
bool read_cb(ID3D11DeviceContext* c, ID3D11Buffer* b, void* out, UINT n) {
    if (!b) return false;
    D3D11_BUFFER_DESC d{};
    b->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ID3D11Buffer* st = nullptr;
    if (FAILED(g_dev->CreateBuffer(&d, nullptr, &st))) return false;
    c->CopyResource(st, b);
    D3D11_MAPPED_SUBRESOURCE m;
    bool ok = SUCCEEDED(c->Map(st, 0, D3D11_MAP_READ, 0, &m));
    if (ok) memcpy(out, m.pData, std::min<UINT>(n, d.ByteWidth)), c->Unmap(st, 0);
    st->Release();
    return ok;
}
}  // namespace

void effectfix_log_draw(ID3D11DeviceContext* c) {
    ID3D11Buffer *ps0 = nullptr, *ps3 = nullptr, *vs0 = nullptr, *vs3 = nullptr;
    c->PSGetConstantBuffers(0, 1, &ps0);
    c->PSGetConstantBuffers(3, 1, &ps3);
    c->VSGetConstantBuffers(0, 1, &vs0);
    c->VSGetConstantBuffers(3, 1, &vs3);
    float inst[28] = {}, lights[36] = {};
    const bool have = read_cb(c, vs0 ? vs0 : ps0, inst, sizeof inst);
    uint32_t spec = 0;
    memcpy(&spec, &inst[20], 4);
    if (have && (spec & (0x20000000u | 0x200u | 0x400u)) && read_cb(c, ps3 ? ps3 : vs3, lights, sizeof lights)) {
        const float* cp = nullptr;
        if (uint8_t* cam = *g_img->va<uint8_t*>(0x143F3A870)) cp = reinterpret_cast<const float*>(cam + 0x2A0);
        log("[licht] spec %08X ambient %.2f %.2f %.2f %.2f Kamera %.2f %.2f %.2f\n", spec, inst[16], inst[17], inst[18],
            inst[19], cp ? cp[0] : 0.f, cp ? cp[1] : 0.f, cp ? cp[2] : 0.f);
        log("[licht]   mv %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f\n",
            inst[0], inst[1], inst[2], inst[3], inst[4], inst[5], inst[6], inst[7], inst[8], inst[9], inst[10], inst[11],
            inst[12], inst[13], inst[14], inst[15]);
        for (int i = 0; i < 3; ++i)
            log("[licht]   %d: pos %.2f %.2f %.2f w %.4f | col %.2f %.2f %.2f w %.2f | falloff %.2f %.2f %.4f %.2f\n", i,
                lights[i * 4], lights[i * 4 + 1], lights[i * 4 + 2], lights[i * 4 + 3], lights[12 + i * 4],
                lights[13 + i * 4], lights[14 + i * 4], lights[15 + i * 4], lights[24 + i * 4], lights[25 + i * 4],
                lights[26 + i * 4], lights[27 + i * 4]);
    }
    for (ID3D11Buffer* b : {ps0, ps3, vs0, vs3})
        if (b) b->Release();
}

}  // namespace host
