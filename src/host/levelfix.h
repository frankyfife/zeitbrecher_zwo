// Robot Factory (Story-Level 9, l_41): Hänger nach dem zweiten Energieknoten
// (TS2 Redux #12/#25). Bisher nur eine Probe zum Nachstellen, keine
// Behebung (docs/host.md "Robot Factory").
//
// TS2_RFTEST="takt:befehl;..." lenkt die Level-Aktualisierung von l_41
// (Tabelle 0x14255AAB0 -> 0x14116F78C) um und führt nach so vielen Aufrufen
// Befehle aus:
//   i            Knoten, Spieler, Kamera, Effekte 6-10, Ring-Speicher, Globale
//   k<n>         Knoten n: Leben 0, Todesrückruf (0x14116F600) direkt
//   d<n>[/s]     Knoten n über TS2s Schadensweg (0x1411E810C) treffen
//   s[/s]        alle Schildgeneratoren über den Schadensweg zerstören
//   x<n>[/art]   n Explosionen (0x14111829C) an Knoten 3
//   t x/y/z[/g]  Spieler versetzen (Raum neu suchen), g = Gier in Grad
//   p<id>/q<id>  Spieler auf Pad id (hex) / Pad-Lage ausgeben
//   n x/z/r      Pads im Umkreis
//   y<g>         Gier setzen
#pragma once
#include "ts2bin.h"

namespace host {

// vor dem Start von TS2 aufrufen
void levelfix_install(const Ts2Image& img);

}  // namespace host
