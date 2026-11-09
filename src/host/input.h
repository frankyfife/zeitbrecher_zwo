// Eingabe für TS2 – an Stelle von Homefronts Action-Map "radical".
//
// Der PC-Port nimmt Eingaben nur über Methoden seines Laufzeitobjekts an
// (docs/host.md, "Eingabe"); TS2s scePadRead (0x141225898) baut daraus die
// Pad-Daten. Tastatur+Maus laufen im Mausmodus (Port 6), ein Pad (XInput)
// im Pad-Modus (Port 0); es gilt das zuletzt benutzte Gerät.
#pragma once
#include <windows.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace host {

struct InputConfig;

void input_init(HWND wnd, void* ts2_object, const std::filesystem::path& ini);
// Fensternachrichten (Raw Input, Mausrad, Fokus); true, wenn verarbeitet.
bool input_message(const MSG& m);
// Einmal je Takt vor Tick: Zustand an TS2 geben.
void input_frame();
// Für Tests: feste Tastenbits für einige Takte vorgeben ("ab Takt N Bits B für K Takte").
void input_script(const wchar_t* spec);
// Bits für ts2.exe selbst (erreichen TS2 nicht): Sprung, vorwärts (nur
// Testskripte), Rennen. Vom Pad: rechte Schultertaste Sprung, linke Rennen
// (wie White/Black im Xbox-Fork).
constexpr uint32_t kHostJump = 0x80000000u, kHostForward = 0x40000000u, kHostRun = 0x20000000u,
                   kHostMouseRight = 0x10000000u;  // nur Testskripte: Maus 20 Zählschritte je Takt nach rechts
uint32_t input_host_bits();
// true, solange Tastatur+Maus das Gerät ist (Mausmodus, Port 6)
bool input_uses_mouse();
// Vorgaben für ts2.ini [tasten] (vor input_init: die eingebauten Werte)
void input_default_keys(std::vector<std::pair<std::string, std::string>>* out);
std::string input_binding_version();

}  // namespace host
