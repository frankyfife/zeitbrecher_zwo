// Hauptmenü "Quit"/"Beenden" (docs/host.md, "Beenden").
#pragma once

#include "ts2bin.h"

namespace host {

// Lenkt die Menüaktion "Quit" (0x1410C0D80) auf eigenen Code: Sie setzt wie
// bisher TS2s Beenden-Flag und schließt dann das Fenster wie der Nutzer
// (WM_CLOSE), sodass ts2.exe denselben Weg wie beim Schließen nimmt.
void quit_install(const Ts2Image& img);

}  // namespace host
