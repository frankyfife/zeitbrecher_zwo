// Adressen im TS2-Code (aus Homefront2_Release.exe, TimeDateStamp 0x58E252DC),
// als VA bei ImageBase 0x140000000. Herkunft: docs/funktionen.md, docs/host.md.
#pragma once
#include <cstdint>

namespace hf2 {

// CryEngines globale Umgebung (statische Struktur in .data). TS2 liest die
// Zeigerfelder unten; die Fabrik zusätzlich +0x60 und +0xF8.
constexpr uint64_t kGEnv = 0x1424790C0;
constexpr uint32_t kGEnvFields[] = {0x0C0, 0x0D0, 0x0D8, 0x0F8, 0x128, 0x140, 0x168};
// gEnv+0x60 ist CryPak. Bleibt es null, greift TS2s ce_file (sceOpen
// 0x1412252AC, sceRead, sceWrite, sceLseek, sceClose) selbst auf fopen/fread/
// fseek/ftell/fclose zurück, mit Pfaden "ts2/<name>" relativ zum
// Arbeitsverzeichnis (= game/).

// Fabrik des TS2-Laufzeitobjekts: obj = factory(gEnv, HANDLE* semaphore)
constexpr uint64_t kFactory = 0x14122C2B0;
// vtable-Offsets des Laufzeitobjekts (vtable 0x1421EAE20). Das Geräte-
// Argument ist ein Zeiger auf eine Struktur, deren Feld 0 das ID3D11Device
// ist (TS2 legt ihn in 0x143020AC8 ab), beim Zeichnen ebenso für den
// ID3D11DeviceContext (0x143020AD0).
constexpr uint32_t kStart = 0x08;   // Start(obj, DeviceHolder*)
constexpr uint32_t kRelease = 0x10;
constexpr uint32_t kTick = 0x18;    // Tick(obj, DeviceHolder*)
constexpr uint32_t kRender = 0x20;  // Render(obj, DeviceHolder*, ContextHolder*, ?, ?)

// Render-Auflösung: TS2s Grafik-Initialisierung (0x141069DA0) ruft das
// DX11-Setup 0x14122B78C(1920, 1080) mit festen Werten:
//   0x141069E0B  BA 38 04 00 00   mov edx, 1080
//   0x141069E10  B9 80 07 00 00   mov ecx, 1920
constexpr uint64_t kResHeightImm = 0x141069E0C;  // imm32
constexpr uint64_t kResWidthImm = 0x141069E11;   // imm32

// TS2-Block in .text (für Absturzmeldungen)
constexpr uint64_t kTs2Lo = 0x141069B20, kTs2Hi = 0x141231770;

// Spieler und Bewegung (docs/funktionen.md)
constexpr uint64_t kPlayerTablePtr = 0x142F5EAD8;  // -> Spieler, je 0x12A0 Byte
constexpr uint64_t kPlayerCount = 0x143F27F58;     // int32
constexpr uint32_t kPlayerMoveMode = 0x164;        // 0 Gehen, 2 Fallen
constexpr uint32_t kPlayerSub = 0x1C8;             // -> Objekt, dessen +0x140 der Charakter ist
constexpr uint32_t kCharSub = 0x140;
constexpr uint32_t kCharVerticalSpeed = 0x19CC;    // Einheiten je Takt, nach oben positiv
constexpr uint32_t kCharFallStartY = 0x1A20;       // Höhe zu Beginn des Falls (Fallschaden)
constexpr uint32_t kPlayerPos = 0xFC;               // float[3] x, y (oben), z
constexpr uint32_t kPlayerFlags10 = 0x10;           // Bit 0x800000: fällt (Gehcode setzt es)
constexpr uint32_t kPlayerStance = 0x168;           // Haltung (0 stehen, 1 ducken, …)
constexpr uint32_t kPlayerTopSpeed = 0xCC;          // float[4] Höchstgeschwindigkeit je Haltung
constexpr uint64_t kPlayerNow = 0x143F1A388;        // Spieler, der gerade bewegt wird
constexpr uint64_t kPitchLimit = 0x143F1A390;       // float, Nickgrenze in Grad (Spiel: 40)
// Kamera-Einrichtung (Xbox-Fork: camera_setup(near, far, aspect, fov_grad)):
// baut die Projektion und legt tan(fov/2) und aspect*tan(fov/2) für das
// Ausblenden ab (Kamera *0x143F3A870 +0x348/+0x34C). Zwei Aufrufe: beim
// Einrichten der Kamera (das Spiel merkt sich sein fov an +0x350) und je Bild
// mit dem gemerkten Wert.
constexpr uint64_t kCameraSetup = 0x14107EE0C;
constexpr uint64_t kCameraSetupCallInit = 0x141080C85;
constexpr uint64_t kCameraSetupCallFrame = 0x14108208A;
// Kamera-Globale (Xbox 0x4B2CA0): Zeiger, Modus; Modus 8 baut die Sicht aus
// Position und Blickpunkt (Fall 0x1410828DB der Kamera-Aktualisierung
// 0x141081F28), den Raum nimmt er vom aktuellen Spieler (0x1410828EE,
// mov ecx,[r12+0xD78]). Raumsuche wie in Modus 6: 0x1411BFA08(pos, 1.0,
// Spieler 1 +0xF4), 0 = keiner.
constexpr uint64_t kCamera = 0x143F3A870;
constexpr uint64_t kCameraMode = 0x143F3A888;
constexpr uint64_t kFreecamPos = 0x143F3A8A0;     // float[3]
constexpr uint64_t kFreecamTarget = 0x143F3A8AC;  // float[3]
constexpr uint64_t kFreecamExtra = 0x143F3A8B8;   // 2 Werte -> [Kamera+0x208]+0x30/+0x34
constexpr uint64_t kCamMode8Room = 0x1410828EE;
constexpr uint64_t kFindRoom = 0x1411BFA08;
constexpr uint32_t kCamPos = 0x2A0, kCamDir = 0x310, kCamProj = 0x208, kPlayerRoomArg = 0xF4;
constexpr uint64_t kWalk = 0x1411D9748;             // Bewegungsmodus 0, Gehen
constexpr uint64_t kWalkCall = 0x1411DBE73;         // sein einziger Aufruf (im Dispatcher 0x1411DAF8C)
constexpr uint32_t kPlayerFallSpeedX = 0xDF8;       // Quergeschwindigkeit im Fall, je Takt
constexpr uint32_t kPlayerFallSpeedZ = 0xE00;
constexpr uint64_t kFrameTicks = 0x14259CBE4;      // float, Bildzeit in Takten, 0 in der Pause
// Waffentabelle: 46 Zeilen à 0x240 (dahinter −1), zwei Feuermodi à 0xB0 ab
// +0x58, Schussabstand in Takten bei +0x10/+0x14 des Modus (Xbox USA 0x2C3470)
constexpr uint64_t kWeaponTable = 0x14250C2A8;
constexpr int kWeaponRows = 46;
constexpr uint32_t kWeaponRowSize = 0x240;
constexpr uint64_t kWeaponSwitchRate = 0x1421DA4B0;  // .rdata, 20/7 je Sekunde
constexpr float kWeaponSwitchRateValue = 20.0f / 7.0f;

// Freischaltungen. Die Konsolen-Codes (Tabelle 0x1425078B8, je 0x38 Byte:
// Tastenfolge, Länge, Handler; geprüft von 0x141109E80) schalten Flags um:
//   0x142F2C35C (int)   alles: jede Freischalt-Bedingung gilt als erfüllt
//                       (0x141111408, 0x141111560); Handler 0x141109E04
//   0x142F2C360 (char[6]) 0 alle Story-Level, 1 Arcade, 2 Challenge,
//                       3 (ohne Leser), 4 Cheats/Extras (0x14111164C),
//                       5 Unverwundbarkeit; Abfrage 0x141109DA0(i),
//                       Umschalten 0x141109DB0(i)
constexpr uint64_t kUnlockAll = 0x142F2C35C;
constexpr uint64_t kUnlockFlags = 0x142F2C360;
enum UnlockFlag { kUnlockStory = 0, kUnlockArcade = 1, kUnlockChallenge = 2, kUnlockCheats = 4, kUnlockInvincible = 5 };

// Menüseiten: {Takt, ?, ?, ?, Aufbau(page), …}; die Aufbau-Funktion bekommt
// den Seitenzustand (Titel +0x650, Maße +0x658/+0x65C/+0x660, Flags +0x10).
constexpr uint64_t kMainMenuPage = 0x1424F2D90;
constexpr uint64_t kMainMenuBuild = 0x1410C81B8;    // Original-Aufbau (Seite +0x20)
constexpr uint32_t kPageBuild = 0x20;
constexpr uint64_t kOptionsPage = 0x1424F1D78;      // Aufbau 0x1410C3C60
// Menü-Bausteine (Namen nach TS2Redux, Signaturen aus den Aufrufern)
constexpr uint64_t kMenuCreate = 0x1410DADF8;       // menu* (page*, int höhe)
constexpr uint64_t kMenuAddEntry = 0x1410E05C4;     // entry* (menu*, int, int)
constexpr uint64_t kEntryWidth = 0x1410DE308;       // int (entry*)
constexpr uint64_t kMenuX = 0x1410DE368;            // int (menu*)
constexpr uint64_t kMenuY = 0x1410DE378;            // int (menu*)
constexpr uint64_t kMenuLayout = 0x1410DEEBC;       // void (menu*)
constexpr uint64_t kMenuSelect = 0x1410DE0AC;       // void (menu*, int)
constexpr uint64_t kMenuFinish = 0x1410DD97C;       // void (menu*)
constexpr uint64_t kPageShow = 0x1410B3CCC;         // void (page*, int)
constexpr uint64_t kMenuUpdate = 0x1410DFE4C;      // Menü je Takt aktualisieren (Xbox FUN_000F1D30)
constexpr uint64_t kMenuUpdateCall = 0x1410E05AB;  // sein einziger Aufruf (Schleife über die Menüs einer Seite)
constexpr uint64_t kSetNextPage = 0x1410B4948;      // void (page-Beschreibung*)
constexpr uint64_t kGotoStory = 0x1410C817C, kGotoArcade = 0x1410C8194,
                   kGotoChallenge = 0x1410C81A0, kQuit = 0x1410C0D80;
constexpr uint64_t kMenuYOffset = 0x1421D444C;      // float
constexpr uint64_t kMenuHintOffset = 0x1421D4360;   // float
// Texte: char** Tabelle = [0x1424E2430 + Sprache*8], Sprache = [0x142EE39C0]
constexpr uint64_t kStringTables = 0x1424E2430, kLanguage = 0x142EE39C0;
enum StringId : uint32_t {
    kStrArcade = 0xD30, kStrStory = 0xD48, kStrStoryCoop = 0xD50, kStrOptions = 0xD68,
    kStrChallenge = 0xD70, kStrQuit = 0x42F0, kStrSelectHint = 0x7C0, kStrBackHint = 0x938,
};
// Ton (ce_sound.cpp, docs/host.md "Ton")
constexpr uint64_t kSoundMixer = 0x141227CB4;       // mix(float* out, frames, rate, channels)
constexpr uint64_t kSoundVoices = 0x142FB77E0;      // 80 Stimmen je 0xD8 Byte
constexpr uint64_t kSoundBuffers = 0x142F9B240;     // Puffer je 0x28 Byte (+8 Daten, +0x18 Frames, +0x1C Rate, +0x20 Kanäle)
constexpr uint64_t kMasterVolume = 0x14259CF7C;     // float, s_GameMasterVolume × s_RadicalVolume
constexpr uint64_t kMusicVolume = 0x14259CF8C;      // float, Stimmen mit Flag 0x1000
constexpr uint64_t kEffectsVolume = 0x14259CF90;    // float, übrige Stimmen
constexpr uint64_t kSoundListenerDir = 0x142479550; // float[2] = (0, 1), Bezugsrichtung des Stereo-Pannings
constexpr uint64_t kPlayersInGame = 0x143F35188;    // int; 2 = Koop ("Story Co-op")

}  // namespace hf2
