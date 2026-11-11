// Cheats aus ts2.ini [cheats] (docs/host.md, "Cheats").
#pragma once
#include <filesystem>

#include "ts2bin.h"

namespace host {

struct CheatConfig {
    bool infinite_ammo = false;  // munition: Schüsse verbrauchen nichts
    bool no_cooldown = false;    // kein_cooldown: Schussabstand 1 Takt, Nachladen und Waffenwechsel sofort
};

CheatConfig load_cheat_config(const std::filesystem::path& ini);
void cheats_install(const Ts2Image& img, const CheatConfig& c);
// einmal je Takt, vor der TS2-Logik
void cheats_tick(const Ts2Image& img);

}  // namespace host
