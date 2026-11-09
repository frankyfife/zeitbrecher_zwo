// Eingabe für TS2 – an Stelle von Homefronts Action-Map "radical".
//
// Der PC-Port nimmt Eingaben nur über Methoden seines Laufzeitobjekts an
// (docs/host.md, "Eingabe"); TS2s scePadRead (0x141225898) baut daraus die
// Pad-Daten. Tastatur+Maus laufen im Mausmodus (Port 6), ein Pad (XInput)
// im Pad-Modus (Port 0); es gilt das zuletzt benutzte Gerät.
#pragma once
#include <windows.h>

#include <filesystem>

namespace host {

struct InputConfig;

void input_init(HWND wnd, void* ts2_object, const std::filesystem::path& ini);
// Fensternachrichten (Raw Input, Mausrad, Fokus); true, wenn verarbeitet.
bool input_message(const MSG& m);
// Einmal je Takt vor Tick: Zustand an TS2 geben.
void input_frame();
// Für Tests: feste Tastenbits für einige Takte vorgeben ("ab Takt N Bits B für K Takte").
void input_script(const wchar_t* spec);
// Skript-Bits für ts2.exe selbst: 0x80000000 Sprung, 0x40000000 vorwärts.
uint32_t input_host_bits();

}  // namespace host
