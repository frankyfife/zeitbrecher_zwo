// Leuchttexturen ohne Alphakanal (Laserschranken in Siberia u. a.) wie auf
// der Xbox durchsichtig zeichnen.
//
// Befund (TS2_TEXWATCH, docs/host.md "Transparenz"): Die Laserschranke
// (ob/dam/damlaserdoor.xbr, Textur 2452) ist ein roter Strahl auf Schwarz.
// Auf der Xbox (2452.xbt, DXT3) trägt der Alphakanal den Strahl und seinen
// Schein, der Rest ist Alpha 0. Die PC-Daten (textures/2452.rgb) haben
// überall Alpha 255. TS2 zeichnet die Schranke mit SRC_ALPHA/INV_SRC_ALPHA,
// also deckt das Schwarz den Raum dahinter ab: schwarze Rechtecke mit roten
// Linien. Dasselbe gilt für weitere Leuchttexturen der PC-Daten.
//
// Der Vergleich aller Texturen mit dem Xbox-Image (tools/re/xbox_alpha.py,
// docs/alpha-texturen.md) zeigt: Die betroffenen PC-Texturen sind
// Leuchttexturen, deren Farbe schon mit dem Xbox-Alpha multipliziert ist,
// das Alpha selbst fehlt.
//
// Abhilfe für Zeichnungen mit gewöhnlicher Alpha-Mischung:
//  - Liegt game/xbox_alpha.bin vor (vom Werkzeug aus dem eigenen Image
//    erzeugt), bekommt jede dort genannte Textur (am Inhalts-Hash erkannt)
//    eine Kopie mit dem Xbox-Inhalt (Alpha und Farbe der Xbox).
//  - Sonst (und für Texturen, die nicht in der Tabelle stehen): deckende
//    Textur mit überwiegend schwarzem Hintergrund bekommt eine Kopie, deren
//    Alpha die Helligkeit ist (größter Farbkanal).
// TS2s eigener Shader (Licht, Eckenfarbe, Alphatest) bleibt.
#pragma once
#include <d3d11.h>

#include <filesystem>

namespace host {

// table: game/xbox_alpha.bin (darf fehlen)
bool alphafix_init(ID3D11Device* dev, bool on, const std::filesystem::path& table);
// vor einer Zeichnung mit gewöhnlicher Alpha-Mischung: true, wenn die Kopie
// gebunden wurde (dann nach der Zeichnung alphafix_after aufrufen)
bool alphafix_before(ID3D11DeviceContext* c);
void alphafix_after(ID3D11DeviceContext* c);
// eine neu angelegte Textur kann an der Adresse einer alten liegen
void alphafix_forget(ID3D11Texture2D* tex);

}  // namespace host
