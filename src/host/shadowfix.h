// Schattenflecken unter Kisten, Möbeln und Figuren wie auf der Xbox: eine
// weiche, durchsichtige Abdunklung statt schwarzer Flächen.
//
// Befund (TS2_DRAWDUMP, docs/host.md "Schatten"): Die Flecken sind Vierecke
// knapp über dem Boden mit einer schwarzen Textur, deren Alpha die Form
// trägt (16×16-Klecks, Kistenumriss, runder Fleck unter Fässern usw.;
// Alpha gleich der Xbox-Textur), gezeichnet mit SRC_ALPHA/INV_SRC_ALPHA.
// Die Deckkraft ist Textur-Alpha × Eckenalpha aus dem Vertex-Shader, und die
// stimmt im PC-Port nicht:
//  - Mit Licht (spec-Bit 0x200, z. B. 0x8284) zählt TS2s Vertex-Shader je
//    Lampe 1 zum Alpha (Umgebungs-Alpha 0): Eckenalpha 3, der Fleck ist
//    überall, wo die Textur mehr als ein Drittel Alpha hat, voll schwarz
//    (harte schwarze Flächen unter Tischen, Stühlen, Kisten in Chicago).
//  - Mit Eckenfarbe (spec-Bit 2) liest das Eingabe-Layout an Abstand 40
//    Unsinn (oberstes Byte eines Floats, 0,26 bis 0,75; Kisten in Siberia).
// Abhilfe: Für solche Zeichnungen setzt ts2.exe einen eigenen Pixel-Shader,
// der die Textur mit fester Stärke ausgibt (Alpha · stärke / 0,62).
#pragma once
#include <d3d11.h>

#include <cstdint>

namespace host {

// stärke: Deckkraft in der Mitte des 16×16-Kleckses (Alpha 0,62), alle
// Schattentexturen im selben Verhältnis; 0 = aus. spec: TS2s spec der
// zuletzt gesetzten Instanz (Unterscheidung Levelgeometrie / Objekt)
bool shadowfix_init(ID3D11Device* dev, float strength, const uint32_t* spec);
// vor einer Zeichnung mit gewöhnlicher Alpha-Mischung: true, wenn der eigene
// Shader gesetzt wurde (dann nach der Zeichnung shadowfix_after aufrufen)
bool shadowfix_before(ID3D11DeviceContext* c);
void shadowfix_after(ID3D11DeviceContext* c);
// eine neu angelegte Textur kann an der Adresse einer alten liegen
void shadowfix_forget(ID3D11Texture2D* tex);

}  // namespace host
