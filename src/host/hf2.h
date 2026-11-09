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
constexpr uint64_t kFrameTicks = 0x14259CBE4;      // float, Bildzeit in Takten, 0 in der Pause
constexpr uint64_t kWeaponSwitchRate = 0x1421DA4B0;  // .rdata, 20/7 je Sekunde
constexpr float kWeaponSwitchRateValue = 20.0f / 7.0f;

}  // namespace hf2
