// Teilchen und Bildschirm-Sprites (Feuer, Rauch, Funken, Mündungsfeuer,
// Lichthöfe, Effekte) in der Größe der Konsole.
//
// TS2 rechnet ihre Größe in Bildpunkten einer 480 Zeilen hohen Sicht:
// Größe = Wert · Kamera+0x27C / z. Die Xbox setzt den Faktor auf
// 60/Sichtwinkel · Bildhöhe/480 (PAL 0x36DBA, 0x8F6C2; Konstante 0x220670 =
// 1/480), der PC-Port auf 60/Sichtwinkel · Bildhöhe/1080 (0x14108363B,
// 0x1410BC487; 0x1421D3CDC = 1/1080). Bei 1080 Zeilen sind alle diese
// Sprites 2,25-mal zu klein; die Werte selbst (Mündungsfeuer 600/150,
// Schein 100/150, Teilchenklassen) sind gleich der Xbox. Ebenso zählen die
// Obergrenzen in Pixeln der 480er-Sicht: je Teilchen +0x40 (0x14112F3C0),
// 250 in 0x1410F4494.
//
// Abhilfe (TS2_TEILCHENFIX=0 schaltet ab): beide Schreiber nehmen 1/480,
// die Obergrenzen werden mit Bildhöhe/480 vergrößert.
#pragma once
#include "ts2bin.h"

namespace host {

// height: Render-Höhe (ts2.ini [grafik] hoehe); true, wenn aktiv
bool spritefix_install(const Ts2Image& img, int height);

}  // namespace host
