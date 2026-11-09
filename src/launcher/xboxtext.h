// Spieltexte aus dem eigenen Xbox-Image von TimeSplitters 2 holen (wie
// tools/re/xbox_text.py): default.xbe aus dem XISO lesen, darin die
// Texttabellen (je 2770 32-Bit-Zeiger) über "Arcade" an Eintrag 422 finden,
// die Sprache über Eintrag 2142. Ergebnis: <out_dir>/<code>.bin.
// Nur lesend; die erzeugten Dateien gehören nie ins Repository.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace xboxtext {

struct Result {
    std::vector<std::string> languages;  // geschriebene Codes, z. B. "de"
    std::string error;                    // leer bei Erfolg
};

Result extract(const std::filesystem::path& image, const std::filesystem::path& out_dir);

}  // namespace xboxtext
