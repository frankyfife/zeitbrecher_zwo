// Bildtakt 120 Hz, nativ (ohne Zwischenbilder).
//
// TS2 taktet in ganzen 1/60-s-Takten: der Begrenzer am Bildende (0x14107CC64)
// wartet eine Periode (0x142EE39D0, QPC-Einheiten) und zählt die Takte des
// Bildes (0x14259CBE8); die Zeitfortschreibung 0x141222A30 macht daraus die
// Bildzeiten: 0x14259CBD8 (int), 0x14259CBDC (int, Pause 0), 0x14259CBE0
// (float), 0x14259CBE4 (float, Pause 0), und zählt die Spielzeit in Takten
// (0x142F9A22C alle, 0x142F9A230 ohne Pause).
//
// Mit 120 Hz wartet der Begrenzer 1/120 s. Die Zeitfortschreibung bekommt
// weiter 60-Hz-Takte (Spielzeit und Uhren bleiben richtig), danach gelten:
// Float-Bildzeiten halbe Takte je Bild (Bewegung, Blick, Schwerkraft,
// Animation in Float: flüssig), Integer-Bildzeiten die ganzen 60-Hz-Takte
// (abwechselnd 1 und 0: Zähler laufen wie bei 60 Hz). Sieben Stellen, die
// ganzzahlig durch die Integer-Takte teilen (Zufall je Takt), bekommen bei
// 0 Takten "kein Ereignis" statt einer Division durch null.
#pragma once
#include "ts2bin.h"

namespace host {

void timing_install(const Ts2Image& img, int hz);
// je Bild vor Render
void timing_frame();

}  // namespace host
