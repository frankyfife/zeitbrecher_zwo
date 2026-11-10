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
// Text eines 3D-Menüeintrags ausgeben (Lage float2* in rdx); einziger Aufruf
constexpr uint64_t kMenuText3d = 0x14121DA7C;
constexpr uint64_t kMenuText3dCall = 0x1410DAC81;
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

// Profile und Fortschritt (docs/host.md "Fortschritt"). Tabelle: Zeiger
// 0x143F2F468 auf 16 Profile je 0x20E4 Byte (+0 Name, +0x20 Einstellungen,
// +0xF0 Tasten, +0xD30 Fortschritt/Statistik 0x133C Byte, +0x1950… eigene
// Figuren). Profil von Spieler i: 0x142F2D6F0[i] (−1 = keins). Im
// Fortschritt (P = Profil + 0xD30): Story geschafft P+0xBC8 + Koop*0xC +
// Schwierigkeit*4 (Bit = Story-Index 0–9, beim Levelende für alle
// Schwierigkeiten bis zur gespielten gesetzt, 0x14110B520), Medaillen
// P+0xBD0 + Stufe*0x10 (128 Bit, 0x14111130C).
constexpr uint64_t kProfileTable = 0x143F2F468;     // uint8_t* (16 × 0x20E4)
constexpr uint32_t kProfileSize = 0x20E4, kProfileCount = 16;
constexpr uint32_t kProfileProgress = 0xD30;        // P
constexpr uint32_t kProgressStory = 0xBC8;
constexpr uint64_t kPlayerProfile = 0x142F2D6F0;    // int32[4]
constexpr uint64_t kProfileCreate = 0x14110E7C4;    // int (const char* name), neues Profil
constexpr uint64_t kProfileCreateCall = 0x14106A0D8; // beim Start von TS2 (ein Spieler)
constexpr uint64_t kLevelEnd = 0x14110B520;         // Levelende: Fortschritt eintragen
constexpr uint64_t kLevelEndCalls[] = {0x14110B956, 0x1411B0100, 0x1411C7A70};
// Freischalt-Bedingung {Art, a, b, _, Liste*, c, _} (0x20 Byte; Xbox 0x14)
constexpr uint64_t kCondEval = 0x141111408;         // int (cond*, int)
constexpr uint64_t kMedalAny = 0x14111130C;         // int (challenge, −1): eine Medaille
constexpr uint64_t kChallengeSetOpen = 0x1411116CC; // int (satz)
constexpr uint64_t kChallengeSet = 0x141106D48;     // satz* {_, anzahl, ids[]}
constexpr uint64_t kCharEntry = 0x1410B42D4;        // figur* (id), Bedingung +0x10, nullptr wenn unbekannt
constexpr uint64_t kCharTable = 0x1424EFF50;        // je 0x30 Byte
constexpr uint64_t kModeConds = 0x142508100;        // Arcade-Spielarten, je 0x20 (Xbox 0x2BD828)
constexpr uint64_t kMapConds = 0x142508380;         // Arcade-Karten, je 0x40 (Xbox 0x2BD9C4, je 0x30)
constexpr uint32_t kMapStride = 0x40;
constexpr uint64_t kArcadeMode = 0x142F1270C;       // gewählte Spielart (0x10: eigene Kartenliste)
constexpr uint64_t kMode16Map5 = 0x1425091D0, kMode16Map8 = 0x1425091F0;  // Xbox 0x348D18/0x348D2C
// Freischalt-Abfragen, die Dambuster vereinfacht hat (Xbox-Fassung daneben):
constexpr uint64_t kUnlockModeFn = 0x1411112C8;       // Spielart x: Flag 1      (Xbox 0x1AED90)
constexpr uint64_t kUnlockStoryLevelFn = 0x1411112E4; // (level, schw., koop): Flag 0 oder level<2 (Xbox 0x1AECD0)
constexpr uint64_t kUnlockMapFn = 0x141111618;        // Karte x: Flag 1, x 0 oder 3 (Xbox 0x1AEEB0/0x1AEDB0)
constexpr uint64_t kUnlockCharFn = 0x14111169C;       // Figur: immer        (Xbox 0x1AEE50, Flag 3)
constexpr uint64_t kUnlockChallengeFn = 0x141111718;  // (challenge, satz): immer (Xbox 0x1AEF30)
constexpr int kUnlockChars = 3;                     // Flag 3 (Figuren), im PC-Port ohne Leser
// Aufrufe: Kartenliste blendet Gesperrtes aus (Xbox: ausgegraut), Levelwahl ebenso
constexpr uint64_t kMapListShowCalls[] = {0x1410C1A67, 0x1410C1A6E};
constexpr uint64_t kMapMode16Calls[] = {0x1410C1AEE, 0x1410C2DDC, 0x1410C2DEC, 0x1410C31B3, 0x1410C31F0};
constexpr uint64_t kStoryListShowCall = 0x1410C24FF;

// ---- Untertitel der Zwischensequenzen (src/host/subtitles.cpp)
// Einstellung "Subtitles" (Optionen → Audio/Video, Eintrag 4): 0x142F153FC,
// im Profil Einstellungen +0xF0 (= Profil +0x1E0). Einziger Leser im Spiel
// 0x14110A380 (in 0x141146C80, Zwischensequenz zeichnen: Balken, dann je
// Untertitel {Beginn s, Dauer s, Farbe, Art, Text-Nr.} aus 0x142F3C5F8
// (Anzahl 0x142F3C678, je Sequenz eine feste Tabelle, z. B. Siberia
// 0x142515E50) über 0x14121D740). Vorgabe im neuen Profil (0x1410BD218):
// an nur bei Sprache 0x142EE39C0 ≠ 0, im PC-Port also aus.
constexpr uint64_t kSubtitlesOn = 0x142F153FC;
constexpr uint32_t kSettingsSubtitles = 0xF0;      // in den Einstellungen (Profil +0xF0)
constexpr uint64_t kProfileSettings = 0x14110E7A4; // uint8_t* (): Einstellungen von Spieler 1
constexpr uint64_t kSettingsApply = 0x1410CC994;   // Profil → Einstellungs-Globale
constexpr uint64_t kSettingsApplyStart = 0x14106A159; // Endsprung dorthin beim Start von TS2

// ---- Wachhund (src/host/watchdog.cpp, TS2_WACHHUND=1)
constexpr uint64_t kWatchTickCounter = 0x142EE3DE8;  // uint32, Begrenzer 0x14107CC64 addiert die Takte je Bild
constexpr uint64_t kWatchCodeLo = 0x141069000, kWatchCodeHi = 0x141C60000;  // TS2-Code (Stapelsuche)

// ---- Robot Factory, l_41 (src/host/levelfix.cpp)
constexpr uint64_t kRfLevelTickSlot = 0x14255AAB0;  // Level-Tabelle: Aktualisierung 0x14116F78C
constexpr uint64_t kRfNodes = 0x143F273C0;          // Objekt*[] Energieknoten (0x14116FEE0 trägt ein)
constexpr uint64_t kRfNodeCount = 0x143F273F0;      // int
constexpr uint64_t kRfObjTable = 0x143F27B40;       // {int id; Objekt*} je 16 Byte (0x14114DC18)
constexpr uint64_t kRfObjCount = 0x142F3CB10;       // int
constexpr uint64_t kRfPads = 0x142F5EA70;           // Pad* (je 0x20: +0 id, +0xC Lage)
constexpr uint64_t kRfPadCount = 0x143F1A480;       // int

// ---- HUD, Menüanzeigen, Figurenwahl (src/host/hudfix.cpp)
// Texturen (dx11.cpp des Ports): D3D-Tabelle 0x142FE59A0 (4096 × 0x18: Textur,
// SRV, Sampler), Zähler 0x143020A80, davon System 0x143020A84; Anlegen
// 0x141229FD0. Beschreiber-Bank *0x14259D5E0 (= 0x143020AE8: Tabelle je 0x38,
// Bytes, nächster freier absteigend, Anzahl 1024), angelegt je Level in
// 0x14122C430; Laden nach Texturnummer mit Zwischenspeicher 0x14122C700.
constexpr uint64_t kTexListAdd = 0x14122B41C;      // (D3D-Nummer) an die Liste hängen
constexpr uint64_t kTexList = 0x142FFDB60;         // int32[1024], nirgends gelesen
constexpr uint64_t kTexListCount = 0x143020A90;    // int
constexpr uint32_t kTexListCap = 1024;             // dahinter Zustände, ab 0x142FFEB90 Upload-Warteschlange
constexpr uint64_t kD3DSlots = 0x142FE59A0;        // {Textur*, SRV*, Sampler*}[4096]
constexpr uint64_t kD3DTexCount = 0x143020A80;     // int, wächst nur (Levelwechsel: 0x14122B2B4)
constexpr uint64_t kD3DTexSystem = 0x143020A84;    // int, Plätze darunter bleiben
constexpr uint64_t kD3DCreate = 0x141229FD0;       // int (b, h, ?, ?, Daten, ?) → D3D-Nummer
constexpr uint64_t kD3DCreateCalls[] = {0x14122B35A, 0x14122B44E};
constexpr uint64_t kUploadQueue = 0x142FFEB90;     // {Ziel, SRV, Daten, Zeilenlänge, Mip}[], geleert 0x14122A1D0
constexpr uint64_t kUploadCount = 0x143020A8C;
constexpr uint64_t kRtList = 0x14301FA10;          // int32[]: D3D-Nummern der Render-Ziele (0x14122B334)
constexpr uint64_t kRtListCount = 0x143020A94;
constexpr uint64_t kTexBank = 0x14259D5E0;         // TexBank* (= 0x143020AE8)
constexpr uint64_t kTexDescAlloc = 0x14122C33C;    // int (): Beschreiber holen (leer: frei markierte, Flag 0x800000)
constexpr uint64_t kTexDescFree = 0x14122C918;     // (Beschreiber): nur Flag 0x800000 setzen
constexpr uint64_t kCharModelLoad = 0x1410CE6E0;   // (Spieler, Figur) Figurenwahl: Modell laden
constexpr uint64_t kCharModelLoadCall = 0x1410D06A7;
// HUD 0x141140A00 (einziger Aufruf 0x14114D22B); Spieler *0x143F1A388, sein
// Ziel +0xE48 bekommt im Story-Modus (0x143F2F94C == 4) bei bestimmten Typen
// (+0xC, z. B. 0x2C9) den Gegnerbalken 0x141140590 (20 Segmente, Farben
// 0x142514F18, voll ×1,4, leer ×1,4×0,7; Alpha voll 0x7F, leer 0x6E).
constexpr uint64_t kHudDraw = 0x141140A00;
constexpr uint64_t kHudDrawCall = 0x14114D22B;
constexpr uint64_t kHudPlayer = 0x143F1A388;
constexpr uint32_t kPlayerTarget = 0xE48;
constexpr uint64_t kGameMode = 0x143F2F94C;        // 4 = Story
constexpr uint64_t kEnemyBar = 0x141140590;       // (Gegner, Text, x0, y0, x1, y1, Alpha voll, Alpha leer)
constexpr uint64_t kHudQuad = 0x141088D98;        // (x0, y0, x1, y1, 4 Eckenfarben 0xAARRGGBB)
constexpr uint64_t kEnemyBarQuadCall = 0x141140906;
// Bildschirme mit Text (Rückrufe über 0x1411F3E50, zeichnen oben links in
// Konsolen-Pixeln, dann 0x14122C5D0 → 0x14122B384 kopiert Texturgröße heraus):
// 0x141153FA8, Tastenfeld "Enter code:" 0x141160FD4 (128×64), 0x141161530,
// 0x141170A68. Schriftstile 0x14259C020 (24 × 0x28: Schrift, Maßstab x/y +8/+0xC).
constexpr uint64_t kFontStyles = 0x14259C020;
constexpr uint64_t kScreenCallbacks[] = {0x141153FA8, 0x141160FD4, 0x141161530, 0x141170A68};
struct ScreenCallbackRef { uint64_t lea; int k; };   // lea r8, [rip+Rückruf]
constexpr ScreenCallbackRef kScreenCallbackRefs[] = {
    {0x141158811, 0}, {0x141162ED4, 1}, {0x141162C0C, 2}, {0x141173151, 3}, {0x141173249, 3}};
constexpr uint32_t kHudThunkRva = 0x00048000;      // eigene Seite für Sprünge (in ts2.bin nur reserviert)
constexpr uint64_t kHudRect = 0x143F0F508;         // int32*: +0 links, +4 rechts, +0x14 oben
constexpr uint64_t kHudMarginL = 0x143F28860, kHudMarginT = 0x143F28864, kHudMarginR = 0x143F28868;

}  // namespace hf2
