// Erweiterungen nach dem Vorbild des Xbox-Forks (docs/xbox-referenz.md).
#pragma once
#include <filesystem>

#include "ts2bin.h"

namespace host {

struct Settings {
    int width = 1920, height = 1080;  // Render-Auflösung (TS2 intern: 1920x1080)
    bool fullscreen = false;
    int tick_rate = 60;               // Bildtakt 60 oder 120 ([grafik] takt; Spieler/Kamera nativ)
    bool vsync = false;               // auf den Bildwechsel warten (TS2 taktet selbst)
    int weapon_switch_percent = 100;  // 100 = wie das Spiel
    int rof_percent = 100;            // Feuerrate aller Waffen (10–1000)
    int rof_row[46] = {};             // je Waffenzeile, 0 = wie alle
    bool jump = true;
    int jump_key = 0x20;              // virtueller Tastencode (Leertaste)
    float jump_height = 1.2f;         // Spieleinheiten (Xbox-Fork: 1.2)
    bool run = true;                  // Rennen (Xbox-Fork: Black / Shift)
    int run_key = 0xA0;               // virtueller Tastencode (linke Umschalttaste)
    int run_percent = 160;            // Tempo beim Rennen (100–300)
    bool run_toggle = false;          // Taste schaltet um statt gehalten zu werden
    float pitch_limit = 85.0f;        // Nickgrenze in Grad, solange die Maus zielt (Spiel: 40)
    int fov_percent = 100;            // Sichtfeld in Prozent des Spiels (70–150)
};

// Liest ts2.ini (neben ts2.exe); fehlt sie, wird sie mit Vorgaben angelegt.
Settings load_settings(const std::filesystem::path& ini);

void features_install(const Ts2Image& img, const Settings& s);
// einmal je TS2-Takt, vor der TS2-Logik, im selben Thread
void features_tick(const Ts2Image& img, const Settings& s, void* window);

}  // namespace host
