// Die Optionen von ts2.ini, gemeinsam für ts2.exe und den Launcher.
//
// Jede Option steht hier genau einmal mit Abschnitt, Schlüssel, Vorgabe und
// Kommentar. ts2.exe ergänzt beim Start fehlende Optionen (ini_complete),
// der Launcher liest und schreibt dieselbe Datei. Zeilen, die Nutzer selbst
// geändert oder kommentiert haben, bleiben erhalten.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace ts2opt {

struct IniOption {
    std::string section, key, value, comment;  // comment: Zeile(n) davor ohne ';', \n trennt
    bool only_in_new_section = false;          // nur in einen neu angelegten Abschnitt
};

// Tastenbelegung ([tasten]): Vorgaben wie die PC-Belegung des Xbox-Forks
// (split2-recomp_ff, launcher/src/bindings.cpp). Virtuelle Tastencodes,
// bis zu drei je Aktion; Maustasten 1 links, 2 rechts, 4 Mitte.
struct KeyDefault {
    const char* key;
    const char* label;  // für den Launcher
    int vk[3];
};
const std::vector<KeyDefault>& key_defaults();
constexpr int kBindingVersion = 2;  // [tasten] belegung; ältere Abschnitte stellt ts2.exe einmal um

// Alle Optionen in Dateireihenfolge
const std::vector<IniOption>& ini_options();
// Vorgabe einer Option ("" wenn unbekannt)
std::string default_value(const std::string& section, const std::string& key);
// "1,70" aus bis zu drei Codes
std::string vk_list(const int* vk);
// Ergänzt fehlende Optionen; liefert die Zahl der ergänzten.
int ini_complete(const std::filesystem::path& ini, const std::vector<IniOption>& options = ini_options());

}  // namespace ts2opt
