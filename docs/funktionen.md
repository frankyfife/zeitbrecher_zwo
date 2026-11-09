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
| 0x14106CF4C | 14 382 | lädt die TS2-Paks (`host0:data/*.pak`) | Strings |
| 0x1412252AC … 0x1412256F8 | | `ce_file.cpp` (Dateien über CryPak) | `__FILE__` |
| 0x1412259F8 … 0x141225F20 | | `ce_music.cpp` | `__FILE__` |
| 0x14122695C … 0x1412276A0 | | `ce_sound.cpp`; 0x141226070 liest `s_RadicalVolume` | `__FILE__`, CVar |
| 0x1412284BC, 0x141228630 | | `ce_lan.cpp` (LAN über `CRadicalNetwork`) | `__FILE__` |
| 0x141228F30, 0x141229070 | | `dx11\dlImmediate.cpp` | `__FILE__` |
| 0x14122B214, 0x14122B458 | | `dx11\dx11.cpp` | `__FILE__` |
| 0x14122C008 | 315 | Tick (vtable +0x18) | `"TS Tick"` |
| 0x14122C2B0 | 138 | Fabrik des Laufzeitobjekts | einziger Aufruf von außen |
| 0x14122C400 | 14 | `host0:textures/misc/blank.raw` | Strings |
| 0x14122C700 | 371 | Textur laden (`host0:textures/%04d.%s`) | Strings |

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
