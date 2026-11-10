// Fortschritt wie auf der Xbox (docs/host.md "Fortschritt").
//
// Dambuster hat in der Homefront-Fassung die Freischalt-Abfragen durch Flags
// ersetzt (Story: nur Siberia und Chicago, Karten: nur zwei, Figuren und
// Challenges: alle) und speichert das Profil nicht. ts2.exe setzt die
// Regeln der Xbox-Fassung wieder ein (Bedingungen aus dem Profil, die Flags
// aus [freischalten] schalten wie dort alles frei) und hält das Profil von
// Spieler 1 in einer Datei.
#pragma once
#include <filesystem>

#include "ts2bin.h"

namespace host {

// vor dem Start von TS2 aufrufen (nach unlock_install)
void profile_install(const Ts2Image& img, const std::filesystem::path& file);
// schreibt das Profil, wenn es sich seit dem Laden/letzten Schreiben
// geändert hat (Levelende, Hauptmenü, Programmende)
void profile_save();

}  // namespace host
