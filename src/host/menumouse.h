// Die Menüs mit der Maus (wie im Xbox-Fork, docs/pc-work-plan.md Abschnitt 6).
//
// Jede Liste in TS2s Menüs ist ein "Menü" (das Panel des Xbox-Codes), das
// 0x1410DFE4C(menu) je Takt aktualisiert. Was ein Zeiger braucht, steht darin
// (Koordinaten im 1920×1080-Raum der Oberfläche):
//   +0x08  Seite (deren +0x10 & 2: beschäftigt)   +0x14 & 6  sichtbar und bereit
//   +0x3C, +0x40  Lage                           +0x2C, +0x30  Anfang/Ende des Rumpfs
//   +0x5C  gescrollt um (float)                  +0x3A0  Einträge   +0x3A8  Zeilen
//   +0x3B0 gewählte Zeile
// Eintrag: +0x08 nächster, +0x28 Zeilenanfang, +0x44 Flags (8 nicht wählbar,
// 0x100 im Rumpf), Rechteck +0x9C x, +0xA0 y, +0xA4 Breite, +0xA8 Höhe.
// Über einer Zeile drückt ts2.exe für den Spieler Hoch/Runter, bis TS2 sie
// selbst gewählt hat (Töne, übersprungene Zeilen: alles wie mit Tasten);
// Linksklick auf die gewählte Zeile ist OK, Rechtsklick Zurück.
#pragma once
#include <windows.h>

#include <cstdint>

#include "ts2bin.h"

namespace host {

void menumouse_install(const Ts2Image& img, bool enabled);
// Einmal je Takt aus input_frame (Mausmodus): liefert Tastenbits für TS2
// und ob gerade ein Menü Eingaben nimmt (dann ist die Maus frei).
uint32_t menumouse_frame(HWND wnd, bool focused, bool* menu_active);
// Testskript: "takt:x,y[,l|r];…" in Oberflächen-Koordinaten (1920×1080)
void menumouse_script(const wchar_t* spec);

}  // namespace host
