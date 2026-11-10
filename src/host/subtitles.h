// Untertitel der Zwischensequenzen (docs/host.md "Untertitel").
//
// TS2 zeichnet sie selbst (0x141146C80, mit seiner Schrift), aber nur, wenn
// die Einstellung "Subtitles" an ist (0x14110A380 liest 0x142F153FC). Ein
// neues Profil bekommt sie nur in einer anderen Sprache als Englisch
// (0x1410BD218: an, wenn Sprache 0x142EE39C0 ≠ 0); der PC-Port kennt nur
// Englisch, also waren sie immer aus. ts2.exe setzt sie beim Start laut
// ts2.ini [sprache] untertitel.
#pragma once
#include <filesystem>

#include "ts2bin.h"

namespace host {

// vor dem Start von TS2 aufrufen
void subtitles_install(const Ts2Image& img, const std::filesystem::path& ini);

}  // namespace host
