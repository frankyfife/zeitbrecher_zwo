// Brennendes Logo im ersten Bildschirm (vor dem Hauptmenü) wie auf der Xbox.
//
// Ablauf in TS2 (0x1410B5800, Xbox PAL 0x88CB0, gleiche Logik):
//  1. Alpha-Test-Art 7 (0x14122A620), Schreibmaske nur Alpha (0x14122A6A0
//     mit 0xFFFFFF; die Bits sperren Kanäle), dann die Feuerteilchen
//     (0x14112FA10, Klasse 1: misc/fireb1..8, Mischart 3 = SRC_ALPHA/ONE).
//     Sie zeichnen nur in den Alphakanal des Bildes.
//  2. Schreibmaske nur RGB, misc/ts2glow (heller Schein um die Buchstaben)
//     mit Mischart 7 (DEST_ALPHA/ONE), Eckenfarbe orange 0xFF6400FF
//     (0x14108A41C). Sichtbar ist also Schein × Ziel-Alpha der Teilchen.
//
// Befund (TS2_LOGOPROBE, Vergleich mit xemu): Die Teilchengröße
// (0x14112F3C0: (Alter·[+0x3C] + [+0x38]) · Kamera+0x27C / z, höchstens
// [+0x40]) zählt in Bildpunkten einer 480 Zeilen hohen Sicht; der PC-Port
// setzt Kamera+0x27C mit Bildhöhe/1080 statt /480 (spritefix.h): Die
// Teilchen waren 2,25-mal zu klein (ein Fünftel der Fläche).
// Außerdem setzt die Xbox für Art 7 ALPHAREF 0x7E (GREATER) statt 0,33 und
// lässt den Alpha-Test beim Schein an (ihre Art 0 schaltet ihn nicht ab):
// keine schwachen Säume, kräftige Flecken. Zusammen sah man auf dem PC ein
// Drittel der leuchtenden Fläche mit halber Helligkeit.
//
// Abhilfe: Die Teilchengröße richtet spritefix.cpp für alle Teilchen
// (Faktor Bildhöhe/480 wie Xbox). Hier (TS2_LOGOFIX=0 schaltet ab) wird um
// die beiden Aufrufe in 0x1410B5800 der Alpha-Test wie auf der Xbox gesetzt.
// Dazu die Logos FREE RADICAL und EIDOS wie auf der Xbox (PAL
// 0x88D3B–0x88E2C, im PC-Code entfernt; TS2_FIRMENLOGOS=0 lässt sie weg):
// nach dem Schein Mischart 3, ab Sekunde 1 der Uhr Farbe 0x320A0A00 | a,
// a = min(Uhr − 1, 1) · 115, Lage aus 640×480 mit ×3/×2,25 wie die
// Abblend-Rechtecke derselben Funktion.
//
// Probe: TS2_LOGOPROBE=<n> schreibt beim n-ten Schein-Bild den Bildpuffer
// vor und nach dem Schein (logo_vor_alpha.pgm, logo_nach_farbe.ppm …).
#pragma once
#include <d3d11.h>

#include "ts2bin.h"

namespace host {

// installiert die Abhilfe; true, wenn die Probe läuft (dann über die
// Kontext-Hülle logofix_before/logofix_after rufen)
bool logofix_init(const Ts2Image& img, ID3D11Device* dev);
// vor jeder Zeichnung (Kontext-Hülle): true, wenn danach logofix_after nötig ist
bool logofix_before(ID3D11DeviceContext* c);
void logofix_after(ID3D11DeviceContext* c);

}  // namespace host
