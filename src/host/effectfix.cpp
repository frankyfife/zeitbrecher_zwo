#include "effectfix.h"

#include <windows.h>

#include <d3dcompiler.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "hooks.h"
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

// ---- Raumhelligkeit der Figuren
//
// Figuren und die eigene Waffe nehmen als Umgebungslicht die Farbe ihres
// Raums aus der Tabelle 0x143F33CC0 (float[3] je Raumnummer, gelesen von
// 0x1411CA054; < 0: grau 0,3). Die Xbox kopiert dafür beim Levelstart
// bg/ambient/level<n>_ambient.raw (Raum 1 an 0x143F33CCC). Der PC-Code
// (0x1410F6B74, gerufen von 0x1410F78A4) liest stattdessen die Textfassung:
// bg/level<n>/level<n>.h (Raumnamen -> Nummern) und level<n>_ambient.s
// (Raumname, r, g, b), beide in feste Puffer zu 0x2800 Byte, die
// hintereinander liegen (0x142F197B0, 0x142F1BFB0). Ist die .h länger
// (Chicago 10 469, Level 41 12 346 Byte), fehlt ihr Ende und die Suche
// läuft ohne Nullbyte in die .s weiter: in Chicago endet der Puffer mit
// "ROOMNUM_ak8 15", atoi liest 15, und Raum 15 (b11, Lagerhalle, olivgrau)
// bekommt die rotbraune Farbe von ak8; die Räume dahinter fehlen (grau).
// Abhilfe wie auf der Xbox: nach dem Lesen der Textfassung die .raw darüber.
constexpr uint64_t kAmbientParseCall = 0x1410F7A96;  // in 0x1410F78A4
constexpr uint64_t kAmbientParse = 0x1410F6B74;
constexpr uint64_t kAmbientTable = 0x143F33CCC;      // Raum 1
constexpr size_t kAmbientTableBytes = 0x143F3472C - 0x143F33CC4;
constexpr uint64_t kLevelNumber = 0x143F27F54;
constexpr uint64_t kFileRead = 0x1410B2460;  // (name, Ziel, Höchstgröße) -> gelesene Bytes
using VoidFn = void (*)();
using FileReadFn = int (*)(const char* name, void* dst, int max);
VoidFn g_ambient_parse = nullptr;

void ambient_load() {
    g_ambient_parse();
    char name[96];
    const int level = *g_img->va<int32_t>(kLevelNumber);
    snprintf(name, sizeof name, "host0:bg/ambient/level%d_ambient.raw", level);
    static uint8_t buf[0x1000];
    const int n = reinterpret_cast<FileReadFn>(g_img->va<uint8_t>(kFileRead))(name, buf, int(sizeof buf));
    if (n <= 0 || n % 12 != 0) {
        log("Raumhelligkeit: %s fehlt, Textfassung bleibt\n", name);
        return;
    }
    const size_t bytes = std::min<size_t>(size_t(n), kAmbientTableBytes);
    float* tab = g_img->va<float>(kAmbientTable);
    int changed = 0;
    for (size_t i = 0; i < bytes / 4; i += 3) {
        float v[3];
        memcpy(v, buf + i * 4, 12);
        if (v[0] != tab[i] || v[1] != tab[i + 1] || v[2] != tab[i + 2]) ++changed;
    }
    memcpy(tab, buf, bytes);
    log("Raumhelligkeit: Level %d aus .raw (%d Räume, %d anders als die Textfassung)\n", level, n / 12, changed);
}

// ---- Sichtarten mit Unschärferand: Zielfernrohr 7, Fotokamera 11 (Redux-Issue #21)
//
// Die Sichtart des Spielers (0x143F28880 + Spieler·0x12D8, je Bild aus
// Spielerzustand und Fadenkreuz der Waffe gesetzt, 0x1411D63D4 → 0x14113F0DB;
// gezoomt 4 → 5, 6 → 7) wählt im Dispatcher 0x141140040 (Xbox PAL 0xC8A80)
// die Überlagerung: 5 0x141139504 (rundes Zielfernrohr), 7 0x141139844
// (Scharfschützengewehr gezoomt), 8 0x14113F328, 9/10 im Dispatcher
// (Aufsatz, Balken), 11 0x14113A6BC (Fotokamera); sonst Fadenkreuze
// (0x14113FA90). Nur 7 und 11 (Xbox 0xC2D20, 0xC3840) zeichnen den Rand
// zweimal: einmal abgedunkelt in RGB, dann mit Schreibmaske 0xFFFFFF (nur
// Alpha) Verläufe und Ecken mit Art 2, und mischen am Ende die Kopie des
// vorigen Bilds (0x14122B74C(3)) mit Art 6 (DEST_ALPHA/INV_DEST_ALPHA) über
// die ganze Ansicht (Unschärfe am Rand).
// Der Alpha-Durchgang erwartet ein Ziel-Alpha von 0: Auf der Xbox löscht TS2
// das Bild auf 0 (0x223A0, alle Kanäle); das Löschen je Kamera (0x333F3)
// schreibt mit der Maske 0xFF000000 nur RGB (0x1B5650 beachtet die Maske).
// Der PC-Port löscht je Kamera mit Maske 0 und Farbe | 0xFF (0x14107F589,
// ClearRenderTargetView schreibt ohnehin alle Kanäle), also Alpha 1. Innen
// übernahm der Sucher so das vorige Bild 1:1 (eingefroren, verschmiert beim
// Zoom, nach dem Blitz weiß), am Rand mischte er fast gleichmäßig stark
// (Alpha v² + (1 − v) statt v² für einen Verlauf mit Alpha v) mit hartem
// Sprung zum Inneren. Für Sichtart 7 hat der PC-Port selbst ein Rechteck mit
// Farbe 0 und Art 0 über das Innere gesetzt (0x141139B4F/0x141139B64), bei
// 11 fehlt es.
// ts2.exe setzt in beiden zu Beginn des Alpha-Durchgangs das Alpha der ganzen
// Ansicht auf 0 wie auf der Xbox: Haken auf das Setzen der Maske 0xFFFFFF
// (0x141139B32, 0x14113A977), dann Rechteck 0x14108A334 mit Art 0 und
// Farbe 0 (die Maske lässt RGB unverändert). TS2_KAMERAFIX=0 lässt beides
// wie im PC-Port, TS2_KAMERAFIX=2 nur das Innere der Fotokamera (erste
// Fassung dieser Korrektur, Mitte ± Halbbreite·0,26).
constexpr uint64_t kSightMaskCalls[] = {
    0x141139B32,  // Sichtart 7: in 0x141139844, 0x14122A6A0(0xFFFFFF)
    0x14113A977,  // Sichtart 11: in 0x14113A6BC
};
constexpr uint64_t kSetWriteMask = 0x14122A6A0;
constexpr uint64_t kSetBlendMode = 0x14122A5BC;   // (Art) -> vorige Art
constexpr uint64_t kFillRect = 0x14108A334;       // (x0, y0, x1, y1, Farbe)
constexpr uint64_t kViewInts = 0x143F0F508;       // -> Ansicht (int): [0] x0, [1] x1, [5] y0, [6] y1,
                                                  //    [2] Mitte x, [3], [7] Mitte y, [8]
constexpr uint64_t kSightHalf = 0x14210AC88;      // 0,26
using MaskFn = uint64_t (*)(uint32_t);
MaskFn g_set_mask = nullptr;
bool g_sight_inner_only = false;  // TS2_KAMERAFIX=2

uint64_t sight_alpha_pass(uint32_t mask) {
    const uint64_t r = g_set_mask(mask);
    const int32_t* v = *g_img->va<int32_t*>(kViewInts);
    if (!v) return r;
    int x0 = v[0], y0 = v[5], x1 = v[1], y1 = v[6];
    if (g_sight_inner_only) {
        if (v[8] == 0) return r;
        // wie 0x14113A6F2–0x14113A7D5
        const float c = *g_img->va<float>(kSightHalf);
        const int hx = int(float(v[3]) * c);
        const int hy = int(float(v[3]) / float(v[8]) * (float(v[8]) * c));
        x0 = v[2] - hx, y0 = v[7] - hy, x1 = v[2] + hx, y1 = v[7] + hy;
    }
    const auto art = reinterpret_cast<int (*)(int)>(g_img->va<uint8_t>(kSetBlendMode));
    const int old = art(0);
    reinterpret_cast<void (*)(int, int, int, int, uint32_t)>(g_img->va<uint8_t>(kFillRect))(x0, y0, x1, y1, 0);
    art(old);
    return r;
}

}  // namespace

bool effectfix_install(const Ts2Image& img, ID3D11Device* dev) {
    g_img = &img;
    g_dev = dev;
    wchar_t v[8] = {};
    if (GetEnvironmentVariableW(L"TS2_EFFEKTFIX", v, 8) > 0 && v[0] == L'0') {
        log("Effekte: Korrekturen aus (TS2_EFFEKTFIX=0)\n");
        return false;
    }
    // TS2_RAUMLICHT=0: Raumhelligkeit wie bisher aus der Textfassung
    if (!(GetEnvironmentVariableW(L"TS2_RAUMLICHT", v, 8) > 0 && v[0] == L'0')) {
        g_ambient_parse = reinterpret_cast<VoidFn>(
            hook_call(img, kAmbientParseCall, kAmbientParse, reinterpret_cast<const void*>(&ambient_load)));
        if (!g_ambient_parse) log("Raumhelligkeit: Aufruf 0x1410F7A96 unerwartet, aus\n");
    }
    // TS2_KAMERAFIX=0: Zielfernrohr und Fotokamera wie im PC-Port; 2: nur das
    // Innere der Fotokamera
    const bool sight_env = GetEnvironmentVariableW(L"TS2_KAMERAFIX", v, 8) > 0;
    if (!(sight_env && v[0] == L'0')) {
        g_sight_inner_only = sight_env && v[0] == L'2';
        for (const uint64_t call : kSightMaskCalls) {
            if (g_sight_inner_only && call != kSightMaskCalls[1]) continue;
            void* orig = hook_call(img, call, kSetWriteMask, reinterpret_cast<const void*>(&sight_alpha_pass));
            if (orig) g_set_mask = reinterpret_cast<MaskFn>(orig);
            else log("Sichtarten: Aufruf 0x%llX unerwartet, bleibt wie im PC-Port\n", static_cast<unsigned long long>(call));
        }
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

// TS2_LICHTPROBE=1: Umgebung und Lichter jeder Figurenzeichnung (spec 0x200)
// aus TS2s Vorlage (0x142FE38E0, 0x142FE3850: je Licht Lage, Farbe, min/max),
// gemeldet bei jeder neuen Kombination
static void light_probe() {
    static const bool on = GetEnvironmentVariableW(L"TS2_LICHTPROBE", nullptr, 0) > 0;
    if (!on) return;
    // Raum von Spieler 1 (Spieler +0xD78, wie 0x1411CA054) bei jeder Änderung
    if (uint8_t* p1 = *g_img->va<uint8_t*>(0x142F5EAD8)) {
        static int last_room = -1;
        const int room = *reinterpret_cast<int32_t*>(p1 + 0xD78);
        if (room != last_room) {
            last_room = room;
            const float* pp = reinterpret_cast<const float*>(p1 + 0xFC);
            const float* a = g_img->va<float>(0x143F33CC0) + room * 3;
            log("[lichtprobe] Spieler in Raum %d bei %.1f %.1f %.1f, Umgebung %.3f %.3f %.3f\n", room, pp[0], pp[1],
                pp[2], a[0], a[1], a[2]);
        }
        // TS2_SPRUNG=x,y,z[,raum]: 3 s nach dem Levelstart Spieler 1 dorthin
        // setzen (nur Probe: Raum +0xD78 mitsetzen)
        static float jump[4] = {};
        static int jump_state = [] {
            wchar_t s[64] = {};
            if (GetEnvironmentVariableW(L"TS2_SPRUNG", s, 64) <= 0) return -1;
            jump[3] = -1;
            return swscanf_s(s, L"%f,%f,%f,%f", &jump[0], &jump[1], &jump[2], &jump[3]) >= 3 ? 0 : -1;
        }();
        static ULONGLONG jump_at = 0;
        if (jump_state == 0 && *reinterpret_cast<int32_t*>(p1 + 0xD78) > 2) jump_state = 1, jump_at = GetTickCount64() + 3000;
        if (jump_state == 1 && GetTickCount64() >= jump_at) {
            float* pp = reinterpret_cast<float*>(p1 + 0xFC);
            pp[0] = jump[0], pp[1] = jump[1], pp[2] = jump[2];
            if (jump[3] >= 0) *reinterpret_cast<int32_t*>(p1 + 0xD78) = int32_t(jump[3]);
            log("[lichtprobe] Sprung nach %.1f %.1f %.1f\n", jump[0], jump[1], jump[2]);
            jump_state = 2;
        }
        // TS2_RAUMSUCHE=<raum>: einmal ein Gitter mit der Raumsuche
        // 0x1411BFA08 abtasten und melden, wo dieser Raum liegt
        static int search = [] {
            wchar_t s[16] = {};
            return GetEnvironmentVariableW(L"TS2_RAUMSUCHE", s, 16) > 0 ? _wtoi(s) : -1;
        }();
        if (search >= 0 && *reinterpret_cast<int32_t*>(p1 + 0xD78) > 2) {
            using FindRoomFn = int32_t (*)(const float*, float, float);
            const auto find = reinterpret_cast<FindRoomFn>(g_img->va<uint8_t>(0x1411BFA08));
            const float arg = *reinterpret_cast<float*>(p1 + 0xF4);
            float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
            int hits = 0;
            for (float y = -20; y <= 30; y += 2.5f)
                for (float x = -150; x <= 150; x += 2)
                    for (float z = -150; z <= 150; z += 2) {
                        const float p[3] = {x, y, z};
                        if (find(p, 1.0f, arg) != search) continue;
                        if (hits++ < 12) log("[lichtprobe] Raum %d bei %.0f %.0f %.0f\n", search, x, y, z);
                        for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], p[k]), hi[k] = std::max(hi[k], p[k]);
                    }
            log("[lichtprobe] Raum %d: %d Treffer, %.0f..%.0f %.0f..%.0f %.0f..%.0f\n", search, hits, lo[0], hi[0], lo[1],
                hi[1], lo[2], hi[2]);
            search = -1;
        }
    }
    const uint32_t f = *g_img->va<uint32_t>(kCurSpec);
    if (!(f & 0x200u)) return;
    float v[4 + 36];
    memcpy(v, g_img->va<float>(0x142FE38E0), 16);
    memcpy(v + 4, g_img->va<float>(0x142FE3850), 0x90);
    static std::vector<std::vector<float>> seen;
    // Schlüssel: Umgebung und Farben (die Lagen ändern sich mit der Kamera)
    std::vector<float> k(v, v + 4);
    for (int i = 0; i < 3; ++i) k.insert(k.end(), v + 8 + i * 12, v + 11 + i * 12);
    for (float& x : k) x = float(int(x * 20.f));
    for (auto& s : seen)
        if (s == k) return;
    if (seen.size() > 4000) return;
    seen.push_back(k);
    const float* cp = nullptr;
    if (uint8_t* cam = *g_img->va<uint8_t*>(0x143F3A870)) cp = reinterpret_cast<const float*>(cam + 0x2A0);
    log("[lichtprobe] spec %08X amb %.2f %.2f %.2f %.2f Kamera %.1f %.1f %.1f", f, v[0], v[1], v[2], v[3],
        cp ? cp[0] : 0.f, cp ? cp[1] : 0.f, cp ? cp[2] : 0.f);
    for (int i = 0; i < 3; ++i) {
        const float* l = v + 4 + i * 12;
        log(" | L%d %.1f %.1f %.1f col %.2f %.2f %.2f %.2f f %.1f %.1f", i, l[0], l[1], l[2], l[4], l[5], l[6], l[7],
            l[8], l[9]);
    }
    log("\n");
}

// TS2_ALPHABILD=<a>[:<b>]: vor der a-ten Mischung mit Art 6
// (DEST_ALPHA/INV_DEST_ALPHA) der Sichtarten 7 und 11 (Spieler 1) und danach jeder b-ten das
// Renderziel als alphabild_<n>.bmp speichern, links RGB, rechts Alpha (grau)
static void alpha_dump_probe(ID3D11DeviceContext* c) {
    static int from = -1, step = 0, count = 0;
    static const bool on = [] {
        wchar_t s[32] = {};
        if (GetEnvironmentVariableW(L"TS2_ALPHABILD", s, 32) <= 0) return false;
        return swscanf_s(s, L"%d:%d", &from, &step) >= 1;
    }();
    if (!on) return;
    // nur die erste Zeichnung jeder Mischung mit Art 6, solange Spieler 1
    // Sichtart 7 oder 11 hat (die Menüs mischen ebenfalls mit Art 6)
    static bool was6 = false;
    const bool is6 = *g_img->va<int>(kBlendMode) == 6;
    const bool first = is6 && !was6;
    was6 = is6;
    if (!first) return;
    const int sight = *g_img->va<int32_t>(0x143F28880);
    if (sight != 7 && sight != 11) return;
    // Quelle der Bildkopie: vorige Kopie (0x14122B74C(3)), erst ab der ersten Sichtart gesetzt
    if (*g_img->va<uint64_t>(0x142EE6510) != reinterpret_cast<uint64_t>(g_img->va<uint8_t>(0x142FFD9A8))) return;
    const int n = ++count;
    if (n < from || (n > from && (step <= 0 || (n - from) % step != 0))) return;
    ID3D11RenderTargetView* rtv = nullptr;
    c->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv) return;
    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    rtv->Release();
    ID3D11Texture2D* tex = nullptr;
    if (!res || FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex)))) {
        if (res) res->Release();
        return;
    }
    res->Release();
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    if (d.Format != DXGI_FORMAT_R8G8B8A8_UNORM && d.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
        log("[alphabild] Format %d nicht unterstützt\n", int(d.Format));
        tex->Release();
        return;
    }
    const bool bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM;
    d.Usage = D3D11_USAGE_STAGING, d.BindFlags = 0, d.CPUAccessFlags = D3D11_CPU_ACCESS_READ, d.MiscFlags = 0;
    d.MipLevels = 1, d.ArraySize = 1, d.SampleDesc = {1, 0};
    ID3D11Texture2D* st = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&d, nullptr, &st))) {
        tex->Release();
        return;
    }
    c->CopySubresourceRegion(st, 0, 0, 0, 0, tex, 0, nullptr);
    tex->Release();
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(c->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
        const UINT w = d.Width * 2, h = d.Height, row = (w * 3 + 3) & ~3u;
        std::vector<uint8_t> px(size_t(row) * h);
        for (UINT y = 0; y < h; ++y) {
            const uint8_t* s = static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch;
            uint8_t* o = px.data() + size_t(h - 1 - y) * row;  // BMP: unten zuerst
            for (UINT x = 0; x < d.Width; ++x, s += 4) {
                o[x * 3 + 0] = bgra ? s[0] : s[2];
                o[x * 3 + 1] = s[1];
                o[x * 3 + 2] = bgra ? s[2] : s[0];
                uint8_t* a = o + (d.Width + x) * 3;
                a[0] = a[1] = a[2] = s[3];
            }
        }
        c->Unmap(st, 0);
        // neben ts2.exe (das Arbeitsverzeichnis ist game/)
        wchar_t name[MAX_PATH + 32] = {};
        DWORD len = GetModuleFileNameW(nullptr, name, MAX_PATH);
        while (len > 0 && name[len - 1] != L'\\' && name[len - 1] != L'/') --len;
        swprintf(name + len, 32, L"alphabild_%d.bmp", n);
        if (FILE* f = _wfopen(name, L"wb")) {
            const uint32_t size = 54 + uint32_t(px.size());
            uint8_t hd[54] = {'B', 'M'};
            memcpy(hd + 2, &size, 4);
            hd[10] = 54, hd[14] = 40;
            const int32_t wh[2] = {int32_t(w), int32_t(h)};
            memcpy(hd + 18, wh, 8);
            hd[26] = 1, hd[28] = 24;
            fwrite(hd, 1, 54, f);
            fwrite(px.data(), 1, px.size(), f);
            fclose(f);
            log("[alphabild] alphabild_%d.bmp (%ux%u)\n", n, d.Width, d.Height);
        }
    }
    st->Release();
}

bool effectfix_before(ID3D11DeviceContext* c) {
    fix_rain_blend();
    copy_probe();
    light_probe();
    alpha_dump_probe(c);
    // TS2_SICHTART=<n>: Sichtart des Spielers erzwingen (0x143F28880 +
    // Spieler·0x12D8; 7 = Zielfernrohr 0x141139844, 11 = Fotokamera), nur Probe
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
