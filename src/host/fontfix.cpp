#include "fontfix.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "hooks.h"
#include "log.h"

namespace host {

namespace {

// ---- TS2s Schriften
//
// Vier Schriften ab 0x14259BF60, je 0x30 Byte: +0 Texturname
// ("host0:textures/misc/impact.raw" → ts2/textures/misc/impact.rgb),
// +0x10 Zeichentabelle (je Zeichen ab ' ' 10 Byte: u16 x, y, Breite, Höhe
// in Pixeln der Textur, s8 Versatz nach unten), +0x18 Akzenttabelle
// (Umlaute = Grundzeichen + Akzent aus derselben Textur), +0x20
// Zeilenhöhe, +0x24 Leerzeichen, +0x2C Texturnummer. Die 24 Schriftstile
// (0x14259C020, je 0x28 Byte) wählen mit +0 die Schrift und skalieren mit
// +8/+0xC; die laufende Schrift steht in *0x142F999E0, der Stil in
// *0x142F999E8.
//
// Zeichnen: 0x14121CEC0 geht den Text durch, setzt die Textur der Schrift
// (0x141088C9C → Nummer 0x1424EDFF0, Beschreiber *0x142EE6510) und ruft je
// Zeichen einen Rückruf mit Bildschirm-Rechteck (xmm0–3) und
// Texturrechteck in 1/16 Pixel (u0, v0, u1, v1 auf dem Stapel). Die
// Rückrufe teilen durch 16 × Breite/Höhe des Beschreibers (+0x26/+0x2A),
// also der geladenen Textur:
//   0x14121BEFC  einfacher Text (über 0x14108920C), Aufrufer 0x14121D530
//                (Umriss-Text 0x14121D740: fünf Durchgänge) und 0x14121D5B0
//   0x14121BF68  Text mit Verlauf (0x141089410/0x14108920C), 0x14121D640
//   0x14121C418  3D-Menüs (Pause, Tod, Levelende), 0x14121D9D0
// ts2.exe lenkt die vier lea auf diese Rückrufe auf eigene Hüllen um. Ist
// die gesetzte Textur eine der Schriften und größer als das Original,
// werden u und v mit dem Größenverhältnis multipliziert. Andere Texturen
// (Tastensymbole im Text, Zeichen 0x0B) bleiben unberührt.
constexpr uint64_t kFonts = 0x14259BF60;
constexpr int kFontCount = 4;
constexpr size_t kFontSize = 0x30;
constexpr uint64_t kCurTexNumber = 0x1424EDFF0;  // int
constexpr uint64_t kCurTexDesc = 0x142EE6510;    // Beschreiber*
constexpr size_t kDescWidth = 0x26;               // u16
constexpr size_t kDescHeight = 0x2A;              // u16

// Rückruf: Rechteck auf dem Bildschirm, Texturrechteck in 1/16 Pixel,
// Kontext (Farbe), Schalter
using GlyphFn = void (*)(float, float, float, float, int, int, int, int, void*, int);

struct Site {
    uint64_t lea_va;  // lea rax, [rip+rel32] auf den Rückruf
    uint64_t target;  // Rückruf
    int fn;           // Index in g_orig
};
constexpr Site kSites[] = {
    {0x14121D574, 0x14121BEFC, 0},
    {0x14121D5FD, 0x14121BEFC, 0},
    {0x14121D6AE, 0x14121BF68, 1},
    {0x14121DA06, 0x14121C418, 2},
};

const Ts2Image* g_img = nullptr;
GlyphFn g_orig[3] = {};

struct FontInfo {
    char name[32] = {};
    int w = 0, h = 0;          // Größe des Originals (0: unbekannt)
    int seen_w = 0, seen_h = 0; // zuletzt gemeldete geladene Größe
};
FontInfo g_font[kFontCount];

// Größe einer SGI-Datei aus dem Kopf (big endian: +6 Breite, +8 Höhe)
bool sgi_size(const std::string& path, int* w, int* h) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t head[10] = {};
    const bool ok = std::fread(head, 1, sizeof head, f) == sizeof head && head[0] == 0x01 && head[1] == 0xDA;
    std::fclose(f);
    if (!ok) return false;
    *w = head[6] << 8 | head[7];
    *h = head[8] << 8 | head[9];
    return *w > 0 && *h > 0;
}

// Verhältnis geladene / Original-Größe der gesetzten Textur, wenn sie eine
// Schrift ist; sonst false
bool font_scale(int* num_x, int* den_x, int* num_y, int* den_y) {
    const int cur = *g_img->va<int32_t>(kCurTexNumber);
    if (cur < 0) return false;
    const uint8_t* desc = *g_img->va<uint8_t*>(kCurTexDesc);
    if (!desc) return false;
    for (int i = 0; i < kFontCount; ++i) {
        const uint8_t* font = g_img->va<uint8_t>(kFonts + i * kFontSize);
        if (*reinterpret_cast<const int32_t*>(font + 0x2C) != cur) continue;
        FontInfo& fi = g_font[i];
        if (!fi.w) return false;
        const int w = *reinterpret_cast<const uint16_t*>(desc + kDescWidth);
        const int h = *reinterpret_cast<const uint16_t*>(desc + kDescHeight);
        if (w != fi.seen_w || h != fi.seen_h) {
            fi.seen_w = w;
            fi.seen_h = h;
            if (w != fi.w || h != fi.h)
                log("[schrift] %s: geladen %dx%d, Original %dx%d, Zeichen werden umgerechnet\n", fi.name, w, h,
                    fi.w, fi.h);
        }
        if ((w == fi.w && h == fi.h) || w <= 0 || h <= 0) return false;
        *num_x = w;
        *den_x = fi.w;
        *num_y = h;
        *den_y = fi.h;
        return true;
    }
    return false;
}

template <int I>
void glyph_hook(float x0, float y0, float x1, float y1, int u0, int v0, int u1, int v1, void* ctx, int flag) {
    int nx, dx, ny, dy;
    if (font_scale(&nx, &dx, &ny, &dy)) {
        u0 = int(int64_t(u0) * nx / dx);
        u1 = int(int64_t(u1) * nx / dx);
        v0 = int(int64_t(v0) * ny / dy);
        v1 = int(int64_t(v1) * ny / dy);
    }
    g_orig[I](x0, y0, x1, y1, u0, v0, u1, v1, ctx, flag);
}

constexpr GlyphFn kHooks[3] = {&glyph_hook<0>, &glyph_hook<1>, &glyph_hook<2>};

// lea rax, [rip+rel32] an `va` auf `fn` umlenken, wenn es auf `target` zeigt
bool redirect_lea(uint64_t va, uint64_t target, const void* fn) {
    uint8_t* at = g_img->va(va);
    int32_t rel;
    std::memcpy(&rel, at + 3, 4);
    if (at[0] != 0x48 || at[1] != 0x8D || at[2] != 0x05 || at + 7 + rel != g_img->va(target)) {
        log("[schrift] bei 0x%llX steht kein lea auf 0x%llX, nicht umgelenkt\n", static_cast<unsigned long long>(va),
            static_cast<unsigned long long>(target));
        return false;
    }
    uint8_t* stub = static_cast<uint8_t*>(jump_stub(*g_img, fn));
    if (!stub) return false;
    const int32_t nrel = int32_t(int64_t(stub) - int64_t(at + 7));
    DWORD old;
    VirtualProtect(at + 3, 4, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(at + 3, &nrel, 4);
    VirtualProtect(at + 3, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 7);
    return true;
}

}  // namespace

void fontfix_install(const Ts2Image& img) {
    g_img = &img;
    // Originalgrößen aus den Originaldateien (die Overlay-Dateien unter
    // game/hd haben dieselben Namen, deshalb ts2/... direkt)
    std::string found;
    for (int i = 0; i < kFontCount; ++i) {
        const char* name = *img.va<const char*>(kFonts + i * kFontSize);
        if (!name) continue;
        std::string rel = name;
        if (rel.rfind("host0:", 0) == 0) rel = rel.substr(6);
        const size_t dot = rel.rfind('.');
        if (dot != std::string::npos) rel = rel.substr(0, dot);
        const size_t slash = rel.rfind('/');
        std::snprintf(g_font[i].name, sizeof g_font[i].name, "%s",
                      (slash == std::string::npos ? rel : rel.substr(slash + 1)).c_str());
        if (sgi_size("ts2/" + rel + ".rgb", &g_font[i].w, &g_font[i].h)) {
            g_font[i].seen_w = g_font[i].w;
            g_font[i].seen_h = g_font[i].h;
            char buf[64];
            std::snprintf(buf, sizeof buf, "%s%s %dx%d", found.empty() ? "" : ", ", g_font[i].name, g_font[i].w,
                          g_font[i].h);
            found += buf;
        }
    }
    int n = 0;
    for (const Site& s : kSites) {
        if (!g_orig[s.fn]) g_orig[s.fn] = reinterpret_cast<GlyphFn>(img.va<void>(s.target));
        n += redirect_lea(s.lea_va, s.target, reinterpret_cast<const void*>(kHooks[s.fn]));
    }
    log("[schrift] Zeichen-Rückrufe %d/4 umgelenkt; Originale: %s\n", n, found.c_str());
}

}  // namespace host
