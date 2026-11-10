// Bildschirmeffekte des PC-Ports (TS2-Redux-Issues #3, #21, #4).
//
// Regentropfen auf der Kamera (#3): Die Tropfen (0x141101158, Textur
// misc/camsplash) werden zweimal gezeichnet, erst mit TS2s Mischart 8
// (Ziel·(1 + Quelle): aufhellen), dann versetzt mit Mischart 9. Der PC-Port
// legt Art 9 als DEST_COLOR/ONE mit SUBTRACT an, also Quelle·Ziel − Ziel =
// Ziel·(Quelle − 1): überall, wo die Textur nicht weiß ist, negativ und damit
// schwarz – die schwarzen Quadrate. Gemeint ist das Abdunkeln
// Ziel·(1 − Quelle) (wie auf der PS2: (0 − Cd)·As + Cd); ts2.exe setzt für
// Art 9 dieselben Zustände mit REV_SUBTRACT ein. Art 9 benutzt nur der Regen.
//
// Licht (#4): TS2s Pixel-Shader rechnet Mündungsfeuer und brennende Gegner
// auf der Levelgeometrie gegen SV_POSITION (Bildschirmpixel) und ohne
// Dämpfung; der Vertex-Shader (Figuren) gegen die schon projizierte Lage.
// ts2.exe setzt für diese Zeichnungen ein nachgebautes Shader-Paar mit
// richtigen Lagen und der Dämpfung aus falloffs ein (effectfix.cpp).
// TS2_LICHTFIX=0 aus, 1 nur Level, 2 nur Figuren; TS2_EFFEKTFIX=0 alles aus.
//
// Raumhelligkeit der Figuren: die Tabelle der Raumfarben kommt wie auf der
// Xbox aus bg/ambient/level<n>_ambient.raw statt aus der Textfassung, die der
// PC-Code wegen eines zu kleinen Puffers falsch zuordnet (Chicago: rote
// Figuren und Waffe). TS2_RAUMLICHT=0 aus.
//
// Proben: TS2_LICHTPROBE=1 (Umgebung/Lichter jeder Figurenzeichnung, Raum
// von Spieler 1), dazu TS2_SPRUNG=x,y,z[,raum] (Spieler 3 s nach Levelstart
// versetzen) und TS2_RAUMSUCHE=<raum> (Lage eines Raums suchen);
// TS2_KOPIEPROBE=1 (Bildschirmkopien), TS2_SICHTART=<n> (Sichtart
// erzwingen, 7 = Kamera-Sucher), mit TS2_DRAWDUMP die Mischart und Lichter.
#pragma once
#include <d3d11.h>

#include "ts2bin.h"

namespace host {

// false: alles aus (TS2_EFFEKTFIX=0)
bool effectfix_install(const Ts2Image& img, ID3D11Device* dev);
// vor jeder Zeichnung (Kontext-Hülle): true, wenn danach effectfix_after nötig ist
bool effectfix_before(ID3D11DeviceContext* c);
void effectfix_after(ID3D11DeviceContext* c);
// TS2s aktuelle Mischart (0x143020A98, Index in seine Tabelle der
// Blend-Zustände), für Proben
int effectfix_blend_mode();
// Proben: alle Mischarten TS2s mit ihrem D3D11-Zustand ins Protokoll
void effectfix_log_blend_modes();
// Proben (TS2_DRAWDUMP): Lichter und spec der Zeichnung, wenn sie Licht benutzt
void effectfix_log_draw(ID3D11DeviceContext* c);

}  // namespace host
