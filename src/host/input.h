// Eingabe für TS2 – an Stelle von Homefronts Action-Map "radical".
//
// Der PC-Port nimmt Eingaben nur über Methoden seines Laufzeitobjekts an
// (docs/host.md, "Eingabe"); TS2s scePadRead (0x141225898) baut daraus die
// Pad-Daten. Tastatur+Maus laufen im Mausmodus (Port 6), ein Pad (XInput)
// im Pad-Modus (Port 0); es gilt das zuletzt benutzte Gerät. Die Pad-
// Belegung steht in ts2.ini [pad]; Testskript: Umgebung TS2_PADSKRIPT.
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
// Testskripte), Rennen. Vom Pad nach [pad] springen/rennen (Vorgabe RB/LB,
// wie White/Black im Xbox-Fork).
constexpr uint32_t kHostJump = 0x80000000u, kHostForward = 0x40000000u, kHostRun = 0x20000000u,
                   kHostMouseRight = 0x10000000u;  // nur Testskripte: Maus 20 Zählschritte je Takt nach rechts
uint32_t input_host_bits();
// Flanke schaltet die freie Kamera um (Testskripte; Pad: [pad] freecam, L3+R3)
constexpr uint32_t kHostFreecam = 0x08000000u;
// Testskripte: Mausrad eine Raste hoch / runter
constexpr uint32_t kHostWheelUp = 0x04000000u, kHostWheelDown = 0x02000000u;
// Freie Kamera: solange an, bekommt TS2 keine Bewegung, keinen Blick und von
// den Tasten nur Start (Pause); das hier sammelt sie für die Kamera.
struct FreecamInput {
    float move_x = 0, move_y = 0;  // -1..1: rechts, vorwärts
    float look_x = 0, look_y = 0;  // Maus-Zählschritte (Pad: umgerechnet), y nach unten positiv
    float up = 0;                  // +1 steigen (Leertaste), -1 sinken (C)
    bool fast = false;             // Umschalttaste / LB oder RB
};
void input_set_freecam(bool on);
FreecamInput input_freecam();
// true, solange Tastatur+Maus das Gerät ist (Mausmodus, Port 6)
bool input_uses_mouse();
// nach Größen- oder Lageänderung des Fensters: Mausbereich anpassen
void input_reclip();
// je Takt: TS2s Rüttelmotoren (kleiner/großer Motor, je 0..127); ts2.exe
// gibt sie ans Pad, solange es das Gerät ist ([pad] vibration)
void input_rumble(int small_motor, int large_motor);


}  // namespace host
