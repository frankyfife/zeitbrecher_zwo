// Erweiterungen nach dem Vorbild des Xbox-Forks (docs/xbox-referenz.md).
#pragma once
#include <filesystem>

#include "ts2bin.h"

namespace host {

struct Settings {
    int width = 1920, height = 1080;  // Render-Auflösung (TS2 intern: 1920x1080)
    bool fullscreen = false;
    bool vsync = false;               // auf den Bildwechsel warten (TS2 taktet selbst)
    int weapon_switch_percent = 100;  // 100 = wie das Spiel
    bool jump = true;
    int jump_key = 0x20;              // virtueller Tastencode (Leertaste)
    float jump_height = 1.2f;         // Spieleinheiten (Xbox-Fork: 1.2)
};

// Liest ts2.ini (neben ts2.exe); fehlt sie, wird sie mit Vorgaben angelegt.
Settings load_settings(const std::filesystem::path& ini);

void features_install(const Ts2Image& img, const Settings& s);
// einmal je TS2-Takt, vor der TS2-Logik, im selben Thread
void features_tick(const Ts2Image& img, const Settings& s, void* window);

}  // namespace host
