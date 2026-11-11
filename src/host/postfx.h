// Bildanpassung als letzter Schritt vor der Ausgabe ([grafik] gamma,
// kontrast, helligkeit, saettigung). Neutral (Vorgabe) entfällt der Schritt
// ganz: TS2 zeichnet wie bisher direkt in den Bildpuffer. Sonst zeichnet TS2
// in eine eigene Textur gleicher Größe, und ein Vollbild-Dreieck schreibt sie
// angepasst in den Bildpuffer. Alle Zustände, die dieser Schritt setzt, werden
// danach wiederhergestellt (TS2 kann gesetzte Zustände zwischenspeichern).
// Grundlage für eine spätere HDR-Ausgabe.
//
// Supersampling (supersample.h): Die Textur hat dann die k-fache Größe, und
// der Schritt rechnet sie gefiltert auf die Größe des Bildpuffers (Fenster
// bzw. Monitor): verkleinern mit Flächenfilter (k = 2 auf 1080 Zeilen: Mittel
// aus 2×2, auf 2160 Zeilen 1:1), vergrößern mit Catmull-Rom.
#pragma once
#include <d3d11.h>

namespace host {

struct PostFxSettings {
    float gamma = 1.0f, contrast = 1.0f, brightness = 0.0f, saturation = 1.0f;
    // HDR (scRGB-Bildpuffer): Weiß des Spiels in Vielfachen von 80 Nits
    // ([grafik] hdr_weiss / 80); 0 = SDR
    float hdr_scale = 0.0f;
    bool neutral() const {
        return gamma == 1.0f && contrast == 1.0f && brightness == 0.0f && saturation == 1.0f && hdr_scale == 0.0f;
    }
};

// Legt Textur und Shader an. Liefert false, wenn der Schritt entfällt
// (neutral oder Fehler); dann bleiben scene_rtv/scene_tex unverändert.
// scene_w/scene_h: Größe der Szene (0: wie der Bildpuffer); weicht sie vom
// Bildpuffer ab (Supersampling), entsteht die Textur immer, und die Ausgabe
// wird gefiltert.
bool postfx_init(ID3D11Device* dev, ID3D11Texture2D* backbuffer, const PostFxSettings& s,
                 ID3D11RenderTargetView** scene_rtv, ID3D11Texture2D** scene_tex, UINT scene_w = 0, UINT scene_h = 0);
// Nach TS2s Render: angepasst in den Bildpuffer schreiben (out_w/out_h: seine
// Größe, nur mit Supersampling von Belang; er kann mit dem Fenster wachsen)
void postfx_apply(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* backbuffer_rtv, UINT out_w = 0, UINT out_h = 0);
// Rechteck rect (x0, y0, x1, y1 in Pixeln der Quelle, Größe src_w×src_h)
// gefiltert und ohne Bildanpassung auf das ganze Ziel dst (dst_w×dst_h)
// rechnen; alle Zustände bleiben. Braucht postfx_init mit Supersampling.
bool postfx_resample(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* src, UINT src_w, UINT src_h,
                     const float rect[4], ID3D11RenderTargetView* dst, UINT dst_w, UINT dst_h);

}  // namespace host
