// ts2.ini: alle Optionen an einer Stelle, mit Vorgabe und Kommentar.
//
// Beim Start ergänzt ini_complete, was in der Datei fehlt: fehlende Schlüssel
// am Ende ihres Abschnitts, fehlende Abschnitte am Dateiende. Vorhandene
// Zeilen, Werte und Kommentare bleiben unverändert. So erscheinen neue
// Optionen auch in einer ini aus einer älteren Fassung.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace host {

struct IniOption {
    std::string section, key, value, comment;  // comment: Zeile(n) davor ohne ';', \n trennt
    bool only_in_new_section = false;          // nur in einen neu angelegten Abschnitt
};

// Liste aller Optionen (Reihenfolge = Reihenfolge in der Datei)
std::vector<IniOption> ini_options();
// Ergänzt fehlende Optionen; liefert die Zahl der ergänzten.
int ini_complete(const std::filesystem::path& ini, const std::vector<IniOption>& options);

}  // namespace host
