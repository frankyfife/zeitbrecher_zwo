// Hochaufgelöste Schriften (docs/host.md "Schriften", docs/hd-texturen.md).
//
// TS2 zeichnet Text mit vier Schrift-Texturen (Tabelle 0x14259BF60:
// impact, fontcaps, book, booklight). Die Zeichentabellen geben jedes
// Zeichen in Pixeln der Original-Textur an; die Zeichenrückrufe teilen
// durch die Größe der geladenen Textur. Eine größere Textur aus game/hd
// zeigte deshalb nur ihr oberes linkes Viertel. ts2.exe rechnet die
// Texturkoordinaten der Zeichen auf die geladene Größe um; Lage und Größe
// auf dem Bildschirm bleiben unverändert.
#pragma once

#include "ts2bin.h"

namespace host {

// vor dem Start von TS2 aufrufen (aktuelles Verzeichnis = game-Ordner)
void fontfix_install(const Ts2Image& img);

}  // namespace host
