// Wachhund (TS2_WACHHUND=1): meldet, wo TS2 hängt.
//
// Ein eigener Thread beobachtet TS2s Taktzähler (0x142EE3DE8, vom
// Begrenzer 0x14107CC64 in jedem Bild erhöht). Steht er länger als
// TS2_WACHHUND_MS (Vorgabe 2000) still, hält der Thread den Spiel-Thread
// kurz an und schreibt nach ts2.log: RIP, RSP, die Register, eine rohe
// Suche nach Rücksprungadressen im TS2-Bereich auf dem Stapel und eine
// Häufigkeitsliste der RIP-Werte aus 200 Stichproben (zeigt die Schleife).
// Beim Laden eines Levels kann das auch ohne Hänger anschlagen.
#pragma once
#include "ts2bin.h"

namespace host {

// vom Spiel-Thread vor dem Start von TS2 aufrufen
void watchdog_install(const Ts2Image& img);

}  // namespace host
