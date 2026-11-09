# Funktionsbestimmung

Stand 2026-11-09. Grundlage: Ghidra-12.1.4-Headless-Analyse von
`Homefront2_Release.exe` (94 202 Funktionen, 227 831 direkte Aufrufkanten,
110 734 String-Verweise; Export mit `re/ghidra_scripts/ExportFunctions.java`
nach `re/out/`), ausgewertet mit `tools/re/classify.py`. Die Liste selbst
(`re/out/ts2_functions.tsv`) bleibt lokal, sie wird aus der eigenen EXE
erzeugt:

```bat
python -I tools\re\classify.py
```

## Der TS2-Block

TS2 ist eine eigene, in die EXE gelinkte Bibliothek und liegt als **ein
zusammenhängender Block** in `.text`:

| | |
| --- | --- |
| Beginn | nach `0x141069B20` (letzte CryNetwork-Funktion, `netID`; davor RMI/`SENDBIND`) |
| Ende | vor `0x141231770` (erste CryRender-Funktion, `Load System Shader '%s'...`) |
| Umfang | 4 544 Funktionen, ≈ 1,8 MB Maschinencode |
| darin mit Strings | 192 Funktionen (die übrige Logik hat keine Texte) |
| außerhalb, nur von TS2 genutzt | 3 Funktionen |

Belege für die Ränder: unten beginnt TS2 mit `"TS (%d)"`/Spielernamen
(0x141069D00, 0x14106A01C) und dem Pak-Lader
(`0x14106CF4C`, `host0:data/fe_anim.pak`, `anim.pak`, …, 14 382 Byte); oben
endet es mit der Plattformschicht (`ce_file`, `ce_music`, `ce_sound`,
`ce_lan`, `dx11`, 0x1412252AC–0x14122B458), `"TS Tick"` (0x14122C008), den
Texturladern (`host0:textures/%04d.%s`, 0x14122C700) und einem Satz
Zeichenfunktionen 0x14122C980–0x141231740, die nur von TS2 gerufen werden
und fast nur TS2 rufen. Kein einziger Engine-`__FILE__`-Verweis liegt im Block.

Vorsicht bei Strings wie `UUU?Shadow range`: das sind Float-Konstanten, die
Ghidra als Text gelesen hat (drei Fälle im Block), keine Engine-Belege.

## Schnittstellen

**Homefront → TS2:** genau ein direkter Aufruf in den Block,
`0x14122C2B0` aus `0x14015E820` (dem `Arcade.StartRadical`-Pfad, siehe
[exe-befunde.md](exe-befunde.md)). Das ist die Fabrik des
TS2-Laufzeitobjekts: 0x48 Byte, vtable `0x1421EAE20`, übernimmt zwei
Engine-Zeiger aus ihrem ersten Argument (`+0x60`, `+0xF8`) in die Globalen
`0x143020AC0` und `0x143020AB8`. Alles Weitere ruft Homefront über die
vtable:

| vtable | Funktion | Bemerkung |
| --- | --- | --- |
| +0x00 | 0x14122C270 | vermutlich Destruktor |
| +0x08 | 0x14122BF5C | |
| +0x10 | 0x14122BDE8 | |
| +0x18 | 0x14122C008 | **Tick** (Profiler-Marke `"TS Tick"`): ein Bild/Takt von TS2 |
| +0x20 | 0x14122C144 | |
| +0x28 … +0x88 | 0x14122BE78 … 0x14122BF54 | kleine Abfragen/Setter (je ≈ 35 Byte) |

**TS2 → außen:** 32 Ziele außerhalb des Blocks, fast alle aus der C-Laufzeit
(`0x141C50C74` 191 Aufrufer, `0x141C50C6E` 100, … – memset/memcpy und
Verwandte) und Speicherverwaltung (`0x140001D00`, `0x1400012D0/12F0/1300`).
Die Engine selbst (Dateien, Ton, Renderer, Netz) erreicht TS2 nur über
Schnittstellenzeiger in der Plattformschicht.

Folge für den Port: TS2 ist sauber abgegrenzt. Wer die Fabrik und die
vtable bedient und der Plattformschicht ihre Engine-Schnittstellen gibt,
kann TS2 ohne Homefronts Spielcode betreiben. Das ist der Ansatzpunkt für
den Starter (Schritt 1 der [Roadmap](roadmap.md)) und später für die
eigenständige EXE.

## Bekannte Funktionen

| VA | Größe | Was | Woher |
| --- | --- | --- | --- |
| 0x14107C71C | | Start: initialisiert ~40 Subsysteme (vtable +0x08) | Aufrufstruktur |
| 0x14107CA94 | | Ende (vtable +0x10, danach `delete`) | Aufrufstruktur |
| 0x14107CF44 | | ein Logik-Takt (aus Tick); Zustand `0x142EE3DF0` | Aufrufstruktur |
| 0x14107CFB4 | | ein Bild zeichnen (aus Render) → `0x14107CC64` | Aufrufstruktur |
| 0x14107CC64 | 678 | Bildende und Takt: wartet (Sleep, dann Schleife) auf 1/60 s, setzt die Taktzahl `0x14259CBE8` (1–4) | QPC/Sleep |
| 0x14015F880 | | (Homefront) Übersetzer der Action-Map `radical` in TS2s Tastenbits | Strings `rad_*` |
| 0x14112D4A0 | | Hand-Zustandsautomat der Waffen (Xbox `FUN_000B64C0`); einziger Leser von 20/7 bei `0x1421DA4B0` | Konstante |
| 0x141109DA0 | 15 | Freischalt-Flag `i` abfragen (`0x142F2C360[i]`) | Hauptmenü |
| 0x141109DB0 | | Freischalt-Flag `i` umschalten (mit Ton); Handler der Konsolen-Codes 0x141109E3C … E74 | Code-Tabelle |
| 0x141109E04 | | Code "alles freischalten": schaltet `0x142F2C35C` um, schreibt alles ins Profil "DUFFINS" (0x1411111C8) | Code-Tabelle |
| 0x141109E80 | | prüft die letzten 20 Tasten gegen die Code-Tabelle `0x1425078B8` | Code-Tabelle |
| 0x141111408 | | Freischalt-Bedingung eines Eintrags; immer erfüllt bei `0x142F2C35C` | Flag |
| 0x14111164C | | Cheat `i` (13 Stück, Tabelle `0x142507A58`) frei? Flag 4 schaltet alle frei | Flag |
| 0x1410C81B8 | 847 | Aufbau des Hauptmenüs (Story, Arcade/Challenge nur mit Flag, Quit) | Seite `0x1424F2D90` |
| 0x1410C3C60 | | Aufbau des Optionsmenüs (in der Homefront-Fassung nicht erreichbar) | Seite `0x1424F1D78` |
| 0x1410DADF8, 0x1410E05C4 | | Menü anlegen, Eintrag anhängen (Namen nach TS2Redux) | Aufrufer |
| 0x1410B4948 | | nächste Menüseite setzen | Aufrufer |
| 0x1411C2B38 | | setzt den aktuellen Spieler aus der Spielertabelle | Schreibzugriff |
| 0x1411D9748 | | Bewegungsmodus 0, Gehen (Xbox `FUN_00145300`) | Dispatcher |
| 0x1411DA36C | 958 | Bewegungsmodus 2, Fallen (Xbox `FUN_00145DA0`): Schwerkraft 0,01/Takt, Grenze −0,6, Fallschaden ab 15 | Konstanten |
| 0x1411DAF8C | | Bewegungs-Dispatcher über Spieler +0x164 (Modi 0–4, 6–8) | Aufruf von 0x1411DA36C |
| 0x1411DD4A0 | 2 757 | Blickcode (Xbox `FUN_00148EB0`): Gier +0x154, Nick +0x158, Zielmodus-Bit 1 von +0xC | Konstanten, Offsets |
| 0x14106CF4C | 14 382 | lädt die TS2-Paks (`host0:data/*.pak`) | Strings |
| 0x1412252AC … 0x1412256F8 | | `ce_file.cpp` (Dateien über CryPak) | `__FILE__` |
| 0x1412259F8 … 0x141225F20 | | `ce_music.cpp` | `__FILE__` |
| 0x14122695C … 0x1412276A0 | | `ce_sound.cpp`; 0x141226070 liest `s_RadicalVolume` | `__FILE__`, CVar |
| 0x1412284BC, 0x141228630 | | `ce_lan.cpp` (LAN über `CRadicalNetwork`) | `__FILE__` |
| 0x141228F30, 0x141229070 | | `dx11\dlImmediate.cpp` | `__FILE__` |
| 0x14122B214, 0x14122B458 | | `dx11\dx11.cpp` | `__FILE__` |
| 0x14122C008 | 315 | Tick (vtable +0x18) | `"TS Tick"` |
| 0x14122829C | 247 | Ton starten: 80 Critical Sections, Mixer bei `gEnv+0xD0 ->+0x100` anmelden | Aufrufstruktur |
| 0x141227CB4 | 1 340 | Mixer `mix(float* out, frames, rate, channels)`, Stereo | Rückruf |
| 0x1412276A0 | 838 | `soundPlayBuffer`: Stimme belegen und starten | `__FILE__`-Text |
| 0x1412270E0 | 381 | Position einer Stimme relativ zur Kamera des nächsten Spielers (`[Spieler+8]+0x210`, Position +0x2A0) | Aufrufstruktur |
| 0x141226070 | 321 | Lautstärken aus CVars | Strings |
| 0x14117EF9C, 0x14117F350 | | Ogg ganz laden / als Strom öffnen (vorbisfile) | Aufrufe |
| 0x14117F45C | | Ogg-Strom nachfüllen, am Ende zurückspulen | Aufrufe |
| 0x14122C2B0 | 138 | Fabrik des Laufzeitobjekts | einziger Aufruf von außen |
| 0x14122C400 | 14 | `host0:textures/misc/blank.raw` | Strings |
| 0x14122C700 | 371 | Textur laden (`host0:textures/%04d.%s`) | Strings |

## Daten

| VA | Was |
| --- | --- |
| `0x142F5EAD8` | Zeiger auf die Spielertabelle, je Spieler 0x12A0 Byte; Spieler 1 ist der erste Eintrag |
| `0x143F27F58` | Anzahl der Spieler (int32) |
| `0x143F1A388` | Zeiger auf den Spieler, der gerade bewegt wird (Xbox `0x497340`) |
| `0x14259CBE4` | Bildzeit in Takten (float, Xbox `0x348CEC`); Schwerkraft und Blick werden damit multipliziert |
| `0x1421DA4B0` | Tempo des Waffenwechsels, 20/7 je Sekunde (.rdata) |
| `0x142FB77E0` | 80 Tonstimmen je 0xD8 Byte ([host.md](host.md), "Ton") |
| `0x142F9B240` | Tonpuffer je 0x28 Byte |
| `0x14259CF7C`, `0x14259CF8C`, `0x14259CF90` | Lautstärke gesamt, Musik, Effekte (float, Vorgabe 1,0) |
| `0x142F2C360` | Freischalt-Flags (char[6]): 0 alle Story-Level, 1 Arcade, 2 Challenge, 3 ohne Leser, 4 Cheats, 5 Unverwundbarkeit |
| `0x142F2C35C` | "alles frei" (int): jede Freischalt-Bedingung gilt als erfüllt |
| `0x1425078B8` | Code-Tabelle, 8 Einträge je 0x38 Byte (Tastenfolge u16[20] davor, Länge, Handler) |
| `0x1424E2430` | Texttabellen je Sprache (`0x142EE39C0`); z. B. +0xD30 "Arcade", +0xD68 "Options" |
| `0x1424F2D90` | Seitenbeschreibung Hauptmenü `{Takt, ?, 0, 0, Aufbau, …}`; ebenso Options `0x1424F1D78`, Arcade `0x1424F1B70`, Challenge `0x1424F1AB0`, Story `0x1424F1868` |

Spieler (Offsets im PC-Port; Xbox jeweils 0xC weniger, bis auf +0xC):

| Offset | Feld |
| --- | --- |
| +0x00C | Flags (Bit 1 Zielmodus) |
| +0x148, +0x14C | zwei Floats, die der Dispatcher jeden Takt mit 0,9 dämpft (Xbox +0x13C/+0x140) |
| +0x154 | Gier in Grad |
| +0x158 | Nick in Grad |
| +0x164 | Bewegungsmodus (0 Gehen, 2 Fallen, …) |
| +0x010 | Flags; Bit 0x800000 setzt der Gehcode beim Übergang ins Fallen |
| +0x0FC | Lage x, y (oben), z |
| +0x1C8 | Zeiger; dessen +0x140 ist der Charakter |
| +0xDB8 | Zeiger, den der Fall bei der Landung löscht (Leiter/Wasser?) |
| +0xDF8, +0xE00 | Quergeschwindigkeit im Fall je Takt (Xbox +0x8DC/+0x8E4); wird nie zurückgesetzt und ist vor dem ersten Fall eines Levels Müll |

Charakter: +0x19CC Vertikalgeschwindigkeit (Xbox +0x1338), +0x1A20 Höhe zu
Beginn des Falls für den Fallschaden (Xbox +0x1388).

Die Verschiebung um 0xC ab etwa +0x100 kommt vermutlich von Zeigern im
vorderen Teil der Struktur (8 statt 4 Byte); sie gilt nicht für jedes Feld
und muss je Feld bestätigt werden.

## Nächste Schritte

1. Die übrigen 4 500 Funktionen benennen: von den Xbox-Adressen aus
   ([xbox-referenz.md](xbox-referenz.md)) über gemeinsame Konstanten
   (z. B. 2.857 für den Waffenwechsel, 0.01/−0.6 für die Schwerkraft,
   die Waffentabelle) und Aufrufstruktur; Namen aus der GameCube-Symbolkarte
   (`GTSE4F.map`) übernehmen, wo die Zuordnung sicher ist.
2. Die vtable-Methoden +0x08/+0x10/+0x20 bestimmen (Start, Ende, Rendern?)
   und den Aufbau des Laufzeitobjekts.
3. Die Engine-Schnittstellen der Plattformschicht auflisten (welche
   `ICryPak`-, Ton-, Render-Methoden sie nutzt): das ist der Umfang der
   späteren eigenen Plattformschicht.
4. Namen und Typen in das Ghidra-Projekt zurückschreiben (Skript), damit die
   Arbeit dort weitergeht.
