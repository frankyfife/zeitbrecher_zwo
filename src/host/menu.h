// Freischaltungen und Hauptmenü.
//
// Die Homefront-Fassung zeigt im Hauptmenü nur "Story" und "Quit": Arcade
// und Challenge erscheinen erst, wenn die Flags der Konsolen-Codes gesetzt
// sind (hf2.h, "Freischaltungen"), und "Options" hat Dambuster ganz
// gestrichen. ts2.exe setzt die Flags laut ts2.ini [freischalten] und baut
// das Hauptmenü auf Wunsch selbst, mit "Options".
#pragma once
#include <filesystem>

#include "ts2bin.h"

namespace host {

struct UnlockConfig {
    bool story = true, arcade = true, challenge = true;
    bool cheats = false, invincible = false, all = false;
    bool options_entry = true;  // eigenes Hauptmenü mit "Options"
};

UnlockConfig load_unlock_config(const std::filesystem::path& ini);
// vor dem Start von TS2 aufrufen
void unlock_install(const Ts2Image& img, const UnlockConfig& c);

}  // namespace host
