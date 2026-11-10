// Kistenschatten (und andere Schattenflecken mit derselben Textur) wie auf
// der Xbox: eine schwache, weiche Abdunklung statt eines schwarzen Flecks.
//
// Befund (TS2_DRAWDUMP, docs/host.md "Schatten"): Der PC-Port schreibt in die
// Ecken dieser Flecken an Abstand 32..47 vier Floats (eine Weltlage), das
// Eingabe-Layout liest dort aber die Eckenfarbe (COLOR0, RGBA8 bei 40). Die
// Deckkraft ist damit das oberste Byte eines Floats (0x42 ≈ 0,26 bis
// 0xBF ≈ 0,75, je nach Lage), und der Fleck wird zweimal gezeichnet: fast
// schwarz. Statt der Eckenfarbe nimmt ein eigener Pixel-Shader für
// Zeichnungen mit der Schattentextur (16×16, schwarz, weiches Quadrat in
// Alpha) eine feste Stärke.
#pragma once
#include <d3d11.h>

namespace host {

// stärke: Deckkraft in der Mitte (Textur-Alpha · stärke / max. Textur-Alpha);
// 0 = aus
bool shadowfix_init(ID3D11Device* dev, float strength);
// vor einer Zeichnung mit gewöhnlicher Alpha-Mischung: true, wenn der eigene
// Shader gesetzt wurde (dann nach der Zeichnung shadowfix_after aufrufen)
bool shadowfix_before(ID3D11DeviceContext* c);
void shadowfix_after(ID3D11DeviceContext* c);
// eine neu angelegte Textur kann an der Adresse einer alten liegen
void shadowfix_forget(ID3D11Texture2D* tex);

}  // namespace host
