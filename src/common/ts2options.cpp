#include "ts2options.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace ts2opt {

const std::vector<KeyDefault>& key_defaults() {
    // Reihenfolge = Reihenfolge in ts2.ini. Werte: VK_* (W=87, S=83, A=65,
    // D=68, Strg links=162, Umschalt links=160, Esc=27, Enter=13, Rück=8,
    // Tab=9, Pfeile 37-40, Ziffernblock +/- 107/109).
    static const std::vector<KeyDefault> k = {
        {"vorwaerts", "Vorwärts", {87}},
        {"rueckwaerts", "Rückwärts", {83}},
        {"links", "Links", {65}},
        {"rechts", "Rechts", {68}},
        {"feuer", "Feuer", {1}},
        {"alt_feuer", "Zweitfunktion", {4, 70}},
        {"zoom", "Zielen / Zoom", {2}},
        {"benutzen", "Benutzen", {69}},
        {"nachladen", "Nachladen", {82}},
        {"ducken", "Ducken", {162, 67}},
        {"naechste_waffe", "Nächste Waffe (dazu Mausrad runter)", {}},
        {"vorige_waffe", "Vorige Waffe (dazu Mausrad hoch)", {81}},
        {"zoom_rein", "Zoom rein", {107}},
        {"zoom_raus", "Zoom raus", {109}},
        {"pause", "Pause", {27}},
        {"ok", "Menü: OK", {13}},
        {"zurueck", "Menü: Zurück", {8}},
        {"dreieck", "Dreieck", {9}},
        {"quadrat", "Quadrat", {88}},
        {"menue_hoch", "Menü: hoch", {38}},
        {"menue_runter", "Menü: runter", {40}},
        {"menue_links", "Menü: links", {37}},
        {"menue_rechts", "Menü: rechts", {39}},
    };
    return k;
}

std::string vk_list(const int* vk) {
    std::string s;
    for (int i = 0; i < 3 && vk[i]; ++i) s += (s.empty() ? "" : ",") + std::to_string(vk[i]);
    return s.empty() ? "0" : s;
}

const std::vector<IniOption>& ini_options() {
    static const std::vector<IniOption> all = [] {
        std::vector<IniOption> o = {
            {"waffen", "wechseltempo", "100", "Tempo des Waffenwechsels in Prozent (100 = wie das Spiel; gilt auch fuer die KI)"},
            {"waffen", "feuerrate", "100",
             "Feuerrate in Prozent (200 = doppelt so oft; gilt auch fuer die KI);\n"
             "einzelne Waffen: feuerrate_<zeile>=Prozent (Zeile 0-45 der Waffentabelle)"},
            {"bewegung", "springen", "1", "Springen (Sprungtaste / rechte Schultertaste)"},
            {"bewegung", "sprungtaste", "32", "virtueller Tastencode, 32 = Leertaste"},
            {"bewegung", "sprunghoehe", "1.2", "Sprunghoehe in Spieleinheiten"},
            {"bewegung", "rennen", "1", "Rennen, solange die Taste gehalten wird (linke Schultertaste am Pad)"},
            {"bewegung", "renntaste", "160", "160 = linke Umschalttaste"},
            {"bewegung", "renntempo", "160", "Tempo beim Rennen in Prozent (100-300)"},
            {"bewegung", "rennen_umschalten", "0", "1: die Taste schaltet Rennen ein und aus"},
        };
        bool first = true;
        for (const auto& k : key_defaults()) {
            o.push_back({"tasten", k.key, vk_list(k.vk),
                         first ? "virtuelle Tastencodes (dezimal), bis zu drei je Aktion mit Komma;\n"
                                 "1/2/4 = linke/rechte/mittlere Maustaste. Mausrad: Waffe wechseln."
                               : ""});
            first = false;
        }
        o.push_back({"tasten", "belegung", std::to_string(kBindingVersion), "", true});
        const std::vector<IniOption> rest = {
            {"maus", "empfindlichkeit", "1.0", "Mausempfindlichkeit (1.0 = wie das Spiel)"},
            {"maus", "umkehren", "0", "1: oben und unten vertauscht"},
            {"maus", "nickgrenze", "85", "Nickgrenze in Grad, solange die Maus zielt (Spiel: 40)"},
            {"maus", "menue", "1", "Menues mit der Maus (Zeiger, Linksklick OK, Rechtsklick zurueck)"},
            {"freischalten", "story", "1", "wie die Konsolen-Codes der Homefront-Fassung (1 = an)"},
            {"freischalten", "arcade", "1", ""},
            {"freischalten", "challenge", "1", ""},
            {"freischalten", "cheats", "0", "alle Cheats/Extras im Optionsmenue"},
            {"freischalten", "unverwundbar", "0", ""},
            {"freischalten", "alles", "0", "jede Freischalt-Bedingung gilt als erfuellt (Figuren, Level, Extras)"},
            {"freischalten", "optionen", "1", "Hauptmenue mit \"Options\" (in der Homefront-Fassung gestrichen)"},
            {"ton", "an", "1", ""},
            {"ton", "gesamt", "1.0", "Lautstaerken (1.0 = wie das Spiel)"},
            {"ton", "musik", "1.0", ""},
            {"ton", "effekte", "1.0", ""},
            {"ton", "surround", "1", "5.1 ausgeben, wenn das Geraet es kann"},
            {"sprache", "text", "pc",
             "Spieltexte: pc (englisch, wie im PC-Port), de, fr, it, es, en (britisch);\n"
             "die Texte kommen aus dem eigenen Xbox-Image (Launcher oder tools/re/xbox_text.py)"},
            {"sprache", "xbox_image", "", "Pfad des eigenen Xbox-Images (nur fuer den Launcher)"},
            {"grafik", "breite", "1920", "Render-Aufloesung"},
            {"grafik", "hoehe", "1080", ""},
            {"grafik", "takt", "60", "Bildtakt: 60 oder 120 (120: Spieler und Kamera nativ, Gegner in 60er-Schritten)"},
            {"grafik", "bildsync", "0", "1: zusaetzlich auf den Bildwechsel des Monitors warten (TS2 taktet selbst)"},
            {"grafik", "vollbild", "0", "1: randloses Vollbild beim Start (Alt+Enter schaltet um)"},
            {"grafik", "sichtfeld", "100", "Sichtfeld in Prozent des Spiels (70-150); das Zielfernrohr zoomt im selben Verhaeltnis"},
            {"grafik", "gamma", "1.0", "Bildanpassung (alle neutral = aus): Gamma (>1 heller in den Mitten)"},
            {"grafik", "kontrast", "1.0", "Kontrast (1.0 = wie das Spiel)"},
            {"grafik", "helligkeit", "0.0", "Helligkeit (-0.5 bis 0.5)"},
            {"grafik", "saettigung", "1.0", "Saettigung (1.0 = wie das Spiel)"},
        };
        o.insert(o.end(), rest.begin(), rest.end());
        return o;
    }();
    return all;
}

std::string default_value(const std::string& section, const std::string& key) {
    for (const auto& o : ini_options())
        if (o.section == section && o.key == key) return o.value;
    return "";
}

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void add_option_lines(std::vector<std::string>& out, const IniOption& o) {
    if (!o.comment.empty()) {
        std::istringstream c(o.comment);
        std::string l;
        while (std::getline(c, l)) out.push_back("; " + l);
    }
    out.push_back(o.key + "=" + o.value);
}

}  // namespace

int ini_complete(const std::filesystem::path& ini, const std::vector<IniOption>& options) {
    // Datei zeilenweise (ohne Zeilenende) lesen
    std::vector<std::string> lines;
    bool existed = false;
    {
        std::ifstream f(ini, std::ios::binary);
        if (f) {
            existed = true;
            std::string l;
            while (std::getline(f, l)) {
                if (!l.empty() && l.back() == '\r') l.pop_back();
                lines.push_back(l);
            }
        }
    }
    // Abschnitte: Name -> Index der letzten Zeile mit Inhalt; vorhandene Schlüssel
    std::map<std::string, size_t> section_end;
    std::map<std::string, std::set<std::string>> present;
    std::string cur;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string t = trim(lines[i]);
        if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
            cur = lower(trim(t.substr(1, t.size() - 2)));
            section_end[cur] = i;
            continue;
        }
        if (t.empty()) continue;
        if (!cur.empty()) section_end[cur] = i;
        if (t[0] == ';' || t[0] == '#') continue;
        const size_t eq = t.find('=');
        if (eq != std::string::npos && !cur.empty()) present[cur].insert(lower(trim(t.substr(0, eq))));
    }
    // fehlende Optionen sammeln, je Abschnitt in Listenreihenfolge
    std::vector<std::string> order;
    std::map<std::string, std::vector<const IniOption*>> missing;
    for (const auto& o : options) {
        const std::string s = lower(o.section);
        const bool section_exists = section_end.count(s) > 0;
        if (o.only_in_new_section && section_exists) continue;
        if (section_exists && present[s].count(lower(o.key))) continue;
        if (!missing.count(s)) order.push_back(s);
        missing[s].push_back(&o);
    }
    if (missing.empty()) return 0;
    int added = 0;
    // in vorhandene Abschnitte: hinter deren letzte Inhaltszeile, von hinten
    // nach vorn, damit die gemerkten Indizes gültig bleiben
    std::vector<std::pair<size_t, std::string>> inserts;
    for (const auto& s : order)
        if (section_end.count(s)) inserts.push_back({section_end[s], s});
    std::sort(inserts.begin(), inserts.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (auto& [at, s] : inserts) {
        std::vector<std::string> add;
        for (auto* o : missing[s]) add_option_lines(add, *o), ++added;
        lines.insert(lines.begin() + std::ptrdiff_t(at + 1), add.begin(), add.end());
    }
    // neue Abschnitte ans Ende
    if (!existed) lines.push_back("; Einstellungen fuer ts2.exe");
    for (const auto& s : order) {
        if (section_end.count(s)) continue;
        if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back("");
        lines.push_back("[" + missing[s].front()->section + "]");
        for (auto* o : missing[s]) add_option_lines(lines, *o), ++added;
    }
    std::ofstream f(ini, std::ios::binary | std::ios::trunc);
    for (auto& l : lines) f << l << "\r\n";
    return added;
}

}  // namespace ts2opt
