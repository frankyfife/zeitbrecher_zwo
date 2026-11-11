// Mehrspieler mit geteiltem Bildschirm: bis zu vier Pads (XInput).
//
// TS2 hat alles dafür noch im Code (Spieleranzahl 0x143F35188, Profile je
// Spieler, Kameras mit eigenem Bildausschnitt, HUD je Spieler, Story Co-op);
// die Homefront-Fassung hat nur die Seite zur Wahl der Spieleranzahl
// ("nplayers_page") auf "1 Spieler, weiter" verkürzt und meldet TS2 nur ein
// Eingabegerät. ts2.exe baut die Seite wieder auf (1–4 Spieler, wie auf der
// Xbox), sobald mindestens zwei Pads angeschlossen sind, und gibt TS2 ein
// Pad je Spieler (Port i = Spieler i+1). Im Mehrspieler gelten nur Pads,
// Tastatur und Maus nicht. Mit einem Pad bleibt alles wie bisher.
// docs/host.md, "Mehrspieler".
#pragma once
#include <filesystem>

#include "ts2bin.h"

namespace host {

// vor dem Start von TS2 aufrufen, nach profile_install und features_install
void mp_install(const Ts2Image& img, const std::filesystem::path& ini);
// mehr als ein Spieler gewählt: dann nur Pads, Spieler 1 auf Pad 1
bool mp_active();
int mp_players();
// XInput-Nummer des Pads von Spieler player (0..3), bei der Wahl festgelegt;
// −1 außerhalb des Mehrspielers oder mit Testskript
int mp_pad_user(int player);
// je Takt am Ende von input_frame: Pads der Spieler 2–4 lesen und übersetzen
void mp_frame();

}  // namespace host
