#include "ini.h"

#include <windows.h>

#include "ts2options.h"

namespace {
std::wstring w(const char* s) { return QString::fromUtf8(s).toStdWString(); }
}  // namespace

QString Ini::get(const char* section, const char* key) const {
    const std::wstring def = QString::fromStdString(ts2opt::default_value(section, key)).toStdWString();
    wchar_t buf[512];
    GetPrivateProfileStringW(w(section).c_str(), w(key).c_str(), def.c_str(), buf, 512, path_.c_str());
    return QString::fromWCharArray(buf).trimmed();
}

void Ini::set(const char* section, const char* key, const QString& value) const {
    WritePrivateProfileStringW(w(section).c_str(), w(key).c_str(), value.toStdWString().c_str(), path_.c_str());
}
