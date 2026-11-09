# Referenz: was der Xbox-Fork kann und wie es auf den PC-Port übertragen wird

Quelle: [split2-recomp_ff](https://github.com/frankyfife/split2-recomp_ff)
(archiviert, November 2026), vor allem `src/recomp_manual.c`,
`docs/pc-work-plan.md`, `docs/mouse-aim.md`. Adressen sind die der
**PAL-Xbox-Fassung** (`default.xbe` SHA-1 `2809eb14…`). Der dortige Code
arbeitet auf geliftetem Xbox-Code; übertragbar ist die **Logik**, nicht der
Code: dieselben TS2-Funktionen gibt es im Homefront-Port als x64-Code, ihre
Adressen und (wegen 8-Byte-Zeigern) teils die Strukturoffsets sind andere.
Für jede Funktion unten ist die PC-Entsprechung zu suchen (Spalte "PC").

| Funktion | Xbox (PAL): wo und wie | PC |
| --- | --- | --- |
| Mauszielen | Blickwinkel als Grad-Floats im Spielerobjekt: +0x148 Gier [0,360), +0x14C Nick (−40…40, Grenze global 0x497344). Spielertabelle 0x497268, aktueller Spieler 0x497340. Blickcode `FUN_00148EB0` (rechter Stick × Bildzeit 0x348CEC; nur wenn Bit 3 von +0xC, +0x998 == 0, Bewegungsmodus +0x158 frei). Zielmodus Bit 1 von +0xC (`FUN_00140F40`) | Blickcode **0x1411DD4A0**, Gier +0x154, Nick +0x158 (Xbox +0xC verschoben), Zielmodus-Bit wie Xbox. Maus ist im Homefront-Port schon nativ; Empfindlichkeit war fehlerhaft (TS2Redux: `cl_sensitivity`) |
| Rennen | Gehen `FUN_00145300` (2711 Byte): Vorwärts-/Seitschritt (Arg. 3 und 6), Deckelung pro Haltung (+0xC0 + 4·Haltung, +0x15C). Hook multipliziert Schritte und Deckel (Standard 160 %) | Gehen **0x1411D9748**, einziger Aufruf 0x1411DBE73; Schritte = 3. und 6. Argument, Deckel +0xCC + 4·Haltung (+0x168); **umgesetzt** |
| Springen | Fallen = Bewegungsmodus 2 (`FUN_00145DA0`): ballistisch, quer +0x8DC/+0x8E4 je Tick, hoch Charakter +0x1338, Schwerkraft 0.01/Tick bis −0.6, Fallschaden ab 15 Einheiten (+0x1388). Sprung = Modus 2 vom Boden mit Aufwärtsgeschwindigkeit (1.2 Einheiten Höhe) | Fallen **0x1411DA36C**, Modus Spieler +0x164, Vertikalgeschwindigkeit Charakter +0x19CC; **umgesetzt** mit Quergeschwindigkeit +0xDF8/+0xE00 (Xbox +0x8DC/+0x8E4), im Spiel geprüft |
| Freecam | Kamera `*0x4B2CA0`, Modus 8 von `FUN_00035960` baut die Sicht aus Position/Blickpunkt (0x4B2CC0, 0x4B2CCC). Raum der Kamera (+0x294) per `FUN_00120320(pos, 1.0, player+0xE8)` nachschlagen, sonst verschwindet das Level | PC: Modus `0x143F3A888` = 8, Position `0x143F3A8A0`, Blickpunkt `0x143F3A8AC` (Fall `0x1410828DB` in `0x141081F28`); Raum `0x1411BFA08(pos, 1.0, +0xF4)` wie in Modus 6 `0x14107F8F0` |
| Waffenwechsel | Hand-Zustandsautomat `FUN_000B64C0` (+0x34 Zustand, +0x38 Fortschritt; 1 halten, 2 senken, 3 tauschen, 4/5 Modell, 6 heben). Tempo: ein Float 2.857/s bei 0x00227C10 (nur dort gelesen) | 20/7 bei **0x1421DA4B0**, einziger Leser 0x14112D4A0; **ts2.exe: `wechseltempo` umgesetzt** (ungetestet) |
| Feuerrate | Waffentabelle 0x002C3890 (46 Zeilen à 0x240), zwei Feuermodi à 0xB0 ab +0x58, Schussabstand in Ticks bei +0x10/+0x14 (`FUN_000AFAA0`). PAL-Werte sind 5/6 der USA-Werte (50-Hz-Ticks) | **0x14250C2A8**, gleicher Aufbau, USA-Werte; **umgesetzt** (`feuerrate`) |
| Sichtfeld | `ts2_camera_setup`: fov-Argument von camera_setup skaliert (70–150 %); Breitbild über das Seitenverhältnis | erledigt: `0x14107EE0C`, `[grafik] sichtfeld` |
| 5.1 | Weltklänge sind DirectSound-3D-Stimmen; Verteilung vorn/hinten nach Lage zum Hörer. Musik/Sprache/Menü nur Front | PC-Port nutzt `ce_sound.cpp` (FMOD?): dort 3D-Ausgabe auf 5.1 stellen |
| Bildrate > 60 | Logik bleibt bei 60, Zwischenbilder durch Matrix-Interpolation; Überscan gegen Löcher | PC-Port skaliert Schwerkraft und Blick mit der Bildzeit in Takten (`0x14259CBE4`), hat also eine variable Zeitbasis. Offen: woher sie kommt, ob sie gedeckelt ist und ob alle Systeme (KI, Animation, Feuerrate in ganzen Takten) mitgehen |
| Detailtextur | Schalter 0x47C884, `FUN_001C9792(room bounds, cam+0x27C, 20.0)` | prüfen, ob der PC-Port dasselbe Umschalten zeigt |
| Tickrate PAL | 366 Stellen 1/50 → 1/60 | entfällt (PC-Port hat eigene Zeitbasis), prüfen |
| Menümaus | Panels `FUN_000F1D30(screen, panel)`; Felder +0x34/+0x38 Lage, +0x44/+0x48 Größe, +0x398 Zeile | **umgesetzt**: `0x1410DFE4C(menu)`, Lage +0x3C/+0x40, Zeile +0x3B0, Einträge +0x3A0 ([host.md](host.md)) |

Die Symbolkarte der GameCube-Fassung (`OpenRadical/symbol_maps`,
`ghidra/GTSE4F.map`) gibt die Funktionsnamen der gemeinsamen Codebasis und
hilft, die PC-Funktionen zu benennen (über Strings, Konstanten und
Aufrufstruktur).
