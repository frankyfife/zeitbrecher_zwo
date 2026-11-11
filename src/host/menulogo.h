// Eigener Schein für den Schriftzug im Hauptmenü (docs/hd-texturen.md,
// "Schriftzug im Hauptmenü").
#pragma once

#include <d3d11.h>

#include <filesystem>

#include "ts2bin.h"

namespace host {

// Liegt game/hd/ts2/textures/misc/ts2glow_menue.rgba (tools/hd/logo.py),
// zeichnet das Hauptmenü seinen Schein (misc/ts2glow, 0x1410B5C76) mit
// dieser Textur statt mit ts2glow. ts2glow selbst bleibt unverändert und
// formt weiter das brennende Logo im ersten Bildschirm. true: Hülle nötig.
bool menulogo_init(const Ts2Image& img, ID3D11Device* dev, const std::filesystem::path& game);
// vor/nach jeder Zeichnung (Kontext-Hülle): bindet die eigene Textur
bool menulogo_before(ID3D11DeviceContext* c);
void menulogo_after(ID3D11DeviceContext* c);

}  // namespace host
