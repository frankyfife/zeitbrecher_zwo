// Freischaltungen und Hauptmenü.
//
// Die Homefront-Fassung zeigt im Hauptmenü nur "Story" und "Quit": Arcade
// und Challenge erscheinen erst, wenn die Flags der Konsolen-Codes gesetzt
// sind (hf2.h, "Freischaltungen"), und "Options" hat Dambuster ganz
// gestrichen. ts2.exe setzt die Flags laut ts2.ini [freischalten] und baut
// das Hauptmenü selbst wie auf der Xbox (Arcade und Challenge immer, auf
// Wunsch mit "Options"). Die Flags schalten wie dort alles im jeweiligen
// Bereich frei; ohne sie gilt der Fortschritt im Profil (profile.h).
#pragma once
#include <filesystem>

#include "profile.h"
#include "ts2bin.h"

namespace host {

struct UnlockConfig {
    // Vorgabe: Fortschritt wie auf der Konsole; an = alles frei (Schummeln)
    bool story = false, arcade = false, challenge = false, characters = false;
    bool cheats = false, invincible = false, all = false;
    bool options_entry = true;  // eigenes Hauptmenü mit "Options"
};

UnlockConfig load_unlock_config(const std::filesystem::path& ini);
// vor dem Start von TS2 aufrufen
void unlock_install(const Ts2Image& img, const UnlockConfig& c);
// Spieltexte aus <game>/sprache/<code>.bin (aus dem eigenen Xbox-Image,
// tools/re/xbox_text.py); "pc" oder leer: unverändert
void language_install(const Ts2Image& img, const std::filesystem::path& game, const std::wstring& code);

}  // namespace host
