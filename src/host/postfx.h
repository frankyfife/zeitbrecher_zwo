// Bildanpassung als letzter Schritt vor der Ausgabe ([grafik] gamma,
// kontrast, helligkeit, saettigung). Neutral (Vorgabe) entfällt der Schritt
// ganz: TS2 zeichnet wie bisher direkt in den Bildpuffer. Sonst zeichnet TS2
// in eine eigene Textur gleicher Größe, und ein Vollbild-Dreieck schreibt sie
// angepasst in den Bildpuffer. Alle Zustände, die dieser Schritt setzt, werden
// danach wiederhergestellt (TS2 kann gesetzte Zustände zwischenspeichern).
// Grundlage für eine spätere HDR-Ausgabe.
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
bool postfx_init(ID3D11Device* dev, ID3D11Texture2D* backbuffer, const PostFxSettings& s,
                 ID3D11RenderTargetView** scene_rtv, ID3D11Texture2D** scene_tex);
// Nach TS2s Render: angepasst in den Bildpuffer schreiben
void postfx_apply(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* backbuffer_rtv);

}  // namespace host
