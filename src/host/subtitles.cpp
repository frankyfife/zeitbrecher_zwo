#include "subtitles.h"

#include <windows.h>

#include <cstdint>

#include "hf2.h"
#include "hooks.h"
#include "log.h"

namespace host {

namespace {

using ApplyFn = uint64_t (*)();  // Rückgabe durchreichen (Endsprung)
using SettingsFn = uint8_t* (*)();

ApplyFn g_apply = nullptr;          // 0x1410CC994
SettingsFn g_settings = nullptr;   // 0x14110E7A4
int32_t* g_on = nullptr;           // 0x142F153FC
const int32_t* g_player = nullptr; // 0x142F2D6F0[0]
int g_value = 1;

// Beim Start überträgt TS2 das Profil von Spieler 1 in die Globalen
// (0x1410CC994, Endsprung aus 0x14106A01C). Danach gilt ts2.ini: im Profil
// (damit "Subtitles" im Optionsmenü den Wert zeigt) und in der Globalen, die
// die Zwischensequenz liest. Im Menü umgeschaltet gilt bis zum Beenden.
uint64_t apply_hook() {
    const uint64_t r = g_apply();
    if (*g_player >= 0) *reinterpret_cast<int32_t*>(g_settings() + hf2::kSettingsSubtitles) = g_value;
    *g_on = g_value;
    log("[untertitel] %s (ts2.ini [sprache] untertitel)\n", g_value ? "an" : "aus");
    return r;
}

}  // namespace

void subtitles_install(const Ts2Image& img, const std::filesystem::path& ini) {
    g_value = GetPrivateProfileIntW(L"sprache", L"untertitel", 1, ini.c_str()) != 0;
    g_settings = reinterpret_cast<SettingsFn>(img.va<void>(hf2::kProfileSettings));
    g_on = img.va<int32_t>(hf2::kSubtitlesOn);
    g_player = img.va<const int32_t>(hf2::kPlayerProfile);
    *g_on = g_value;  // falls TS2 vor dem Profil schon zeichnet
    g_apply = reinterpret_cast<ApplyFn>(hook_call(img, hf2::kSettingsApplyStart, hf2::kSettingsApply,
                                                 reinterpret_cast<const void*>(&apply_hook)));
    if (!g_apply) log("[untertitel] Endsprung 0x14106A159 nicht gefunden, bleibt bei der Profil-Vorgabe\n");
}

}  // namespace host
