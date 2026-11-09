// Lesen und Schreiben von ts2.ini über die Windows-Funktionen: sie ändern nur
// die eine Zeile und lassen Kommentare und eigene Zeilen stehen.
#pragma once
#include <QString>

#include <string>

class Ini {
public:
    explicit Ini(std::wstring path) : path_(std::move(path)) {}
    const std::wstring& path() const { return path_; }

    // Wert oder die Vorgabe aus ts2options
    QString get(const char* section, const char* key) const;
    int get_int(const char* section, const char* key) const { return get(section, key).toInt(); }
    double get_double(const char* section, const char* key) const { return get(section, key).toDouble(); }
    bool get_bool(const char* section, const char* key) const { return get_int(section, key) != 0; }

    void set(const char* section, const char* key, const QString& value) const;
    void set_int(const char* section, const char* key, int v) const { set(section, key, QString::number(v)); }
    void set_bool(const char* section, const char* key, bool v) const { set_int(section, key, v ? 1 : 0); }
    void set_double(const char* section, const char* key, double v, int decimals = 2) const {
        set(section, key, QString::number(v, 'f', decimals));
    }

private:
    std::wstring path_;
};
