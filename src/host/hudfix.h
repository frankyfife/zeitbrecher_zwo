// HUD, Menüanzeigen und Figurenwahl (docs/host.md "HUD und Figurenwahl").
#pragma once
#include <filesystem>

#include "ts2bin.h"

namespace host {

// vor dem Start von TS2 aufrufen
void hudfix_install(const Ts2Image& img, const std::filesystem::path& ini);

}  // namespace host
