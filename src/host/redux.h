// Fehlerbehebungen von TS2 Redux (github.com/HFTSRedux/TS2Redux) aus
// game/redux.bin (tools/re/redux_patches.py, aus der eigenen Redux-Kopie).
#pragma once
#include <filesystem>

#include "ts2bin.h"

namespace host {

// Vor allen eigenen Umlenkungen aufrufen: diese prüfen ihre Stellen und
// melden, falls eine Redux-Änderung sie trifft. Gruppen einzeln abschaltbar
// in ts2.ini [redux] (spielfehler, unsichtbar, anzeige, menue).
void redux_install(const Ts2Image& img, const std::filesystem::path& game, const std::filesystem::path& ini);

}  // namespace host
