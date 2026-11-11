// Supersampling ([grafik] supersampling, TS2_SUPERSAMPLING=k hat Vorrang).
//
// TS2 und alle Korrekturen rechnen weiter in der Render-Auflösung (Vorgabe
// 1920×1080: 2D-Matrix, Ansichten, Schere, Löschen, Bildkopien, Proben).
// Darunter zeichnet ts2.exe k-fach so groß:
//  - Bildgroße Ziele werden k-fach angelegt: die Szene (postfx.cpp), TS2s
//    Tiefenpuffer (0x14122B78C) und seine beiden Bildkopien (System-
//    Texturen aus 0x14122B458 über 0x14122B334, Tabelle 0x142FE59A0).
//  - Die Kontext-Hülle (ctxproxy.h) rechnet Ansichten und Scheren beim Setzen
//    auf das gebundene Ziel um und gibt beim Lesen TS2s Werte zurück; Kopien
//    zwischen k-fachen Texturen bekommen k-fache Rechtecke, Kopien aus dem
//    Bild in kleine Texturen (Bildschirme mit Text 0x14122C5D0, Scanner)
//    werden gefiltert verkleinert. Verdeckungsabfragen (0x14123157C) melden
//    Pixel der Render-Auflösung. Linien werden k Pixel breit gezeichnet.
//  - Zum Schluss rechnet postfx.cpp das Bild gefiltert auf die Größe des
//    Fensters bzw. Monitors (der Bildpuffer wächst mit dem Fenster).
// k = 1: alles wie ohne Supersampling (Bildpuffer in Render-Auflösung).
#pragma once
#include <d3d11.h>

namespace host {

// k (1 = aus) und logische Bildgröße (Render-Auflösung); k wird so begrenzt,
// dass die Größe in D3D11 möglich bleibt
void ss_init(float k, UINT w, UINT h);
bool ss_on();
float ss_factor();
UINT ss_width();   // k-fache Breite
UINT ss_height();  // k-fache Höhe

// TS2 legt eine Textur an (Aufruf aus dem TS2-Code): bildgroße Tiefe und
// Bildkopien k-fach. true: desc wurde angepasst (danach ss_register)
bool ss_scale_desc(D3D11_TEXTURE2D_DESC* d);
// diese Textur ist k-fach (die Szene, die eben angelegten)
void ss_register(ID3D11Resource* r);
// eine neu angelegte Textur kann an der Adresse einer alten liegen
void ss_forget(ID3D11Resource* r);
// Maßstab einer Ressource: k-fach angelegt oder 1
bool ss_is_scaled(ID3D11Resource* r);
// Rechteck in Pixeln der Render-Auflösung auf ein k-faches Ziel umrechnen
D3D11_RECT ss_rect(const D3D11_RECT& r);

// Kontext-Hülle (ctxproxy.h); real ist der echte Kontext
void ss_set_targets(ID3D11DeviceContext* real, ID3D11RenderTargetView* rtv0, ID3D11DepthStencilView* dsv);
void ss_set_viewports(ID3D11DeviceContext* real, UINT n, const D3D11_VIEWPORT* v);
void ss_get_viewports(UINT* n, D3D11_VIEWPORT* v);
void ss_set_scissors(ID3D11DeviceContext* real, UINT n, const D3D11_RECT* r);
void ss_get_scissors(UINT* n, D3D11_RECT* r);
void ss_clear_state();
// true: erledigt (sonst wie gerufen weiterleiten)
bool ss_copy_region(ID3D11DeviceContext* real, ID3D11Resource* dst, UINT dsub, UINT dx, UINT dy, UINT dz,
                    ID3D11Resource* src, UINT ssub, const D3D11_BOX* box);
bool ss_copy_resource(ID3D11DeviceContext* real, ID3D11Resource* dst, ID3D11Resource* src);
void ss_update_subresource(ID3D11Resource* dst);
// Ergebnis einer Abfrage: Verdeckung in Pixeln der Render-Auflösung
void ss_get_data(ID3D11Asynchronous* a, void* data, UINT size);

// vor einer Zeichnung: Linien k Pixel breit (Geometrie-Shader); true: danach
// ss_line_after rufen
bool ss_line_before(ID3D11DeviceContext* real);
void ss_line_after(ID3D11DeviceContext* real);

// Bildschirmfoto: Szene (k-fach) auf die Render-Auflösung verkleinert; liefert
// eine Textur in Render-Auflösung (gehört ss) oder nullptr
ID3D11Texture2D* ss_shot(ID3D11DeviceContext* ctx, ID3D11Texture2D* scene);

// TS2_SSPROBE=1: Zeichnungen je Grundform (Punkte, Linien, Dreiecke) alle 600
// Bilder protokollieren
bool ss_probe_on();
void ss_probe_draw(ID3D11DeviceContext* real);
void ss_probe_frame_end();

}  // namespace host
