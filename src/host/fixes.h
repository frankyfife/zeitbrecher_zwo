// Fehlerbehebungen des TS2-Ports nach TS2 Redux (github.com/HFTSRedux/TS2Redux,
// Dank an Fanoto und DevilDwarf): Reaper-Spawn-Absturz, Regen auf der Kamera,
// Unsichtbarkeitsfehler, HUD- und Textanzeige, Menü-Layout wie auf der Konsole.
// Die Stellen stehen als Tabelle in fixes.inc (tools/re/gen_fixes.py); ts2.exe
// braucht dafür weder Redux noch dessen Dateien. Immer an.
#pragma once
#include <filesystem>

#include "ts2bin.h"

namespace host {

// Vor allen eigenen Umlenkungen aufrufen: diese prüfen ihre Stellen danach.
// Liegt game/hd/ vor (hochaufgelöste Texturen, tools/re/hd_textures.py), liest
// TS2 seine Dateien zuerst dort.
void fixes_install(const Ts2Image& img, const std::filesystem::path& game);

}  // namespace host
