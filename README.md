# zeitbrecher_zwo

Arbeitsrepository für einen eigenständigen PC-Port von **TimeSplitters 2**,
aufgebaut auf dem nativen x64-Port, den Dambuster Studios 2016 in
*Homefront: The Revolution* versteckt hat (`Homefront2_Release.exe`).

**Stand:** `ts2.exe` startet TS2 ohne Homefront, CryEngine oder Steam;
Menüs und Level sind mit Tastatur/Maus oder Pad spielbar; Arcade,
Challenge, alle Story-Level und das Optionsmenü sind freigeschaltet. Ton,
Musik und Sprache laufen, auf 5.1-Anlagen räumlich ([docs/host.md](docs/host.md)).

Ziel: Dekompilat und Funktionsbestimmung dieses Ports, daraus ein PC-Port mit
den Funktionen, die der Xbox-Recompile-Fork
([split2-recomp_ff](https://github.com/frankyfife/split2-recomp_ff)) dem Spiel
gegeben hat (Sprung, Rennen, Mauszielen, Freecam, Waffenhandling, Kamera/FOV),
dazu 5.1-Ton, native hohe Bildrate (≥ 120 fps), HDR und ein Launcher im
Retro-Futurismus-Stil. Reihenfolge: [docs/roadmap.md](docs/roadmap.md).

## Rechtliches

Dieses Repository enthält **keine** Daten oder Code des Spiels. Man braucht
eine eigene Kopie von *Homefront: The Revolution* (Steam, App 223100); daraus
richtet `ts2.exe` einmalig `game/` ein. Nie committen: `game/`, `reference/`,
`re/ghidra_project/`, `re/out/` (alles in `.gitignore`). Dekompilierter Code
des Spiels gehört nicht in committete Dateien; Adressen, Namen,
Strukturbeschreibungen und eigener Code schon. Schlüssel und Code werden zur
Laufzeit aus der eigenen EXE gelesen.

## Aufbau

```text
CMakeLists.txt         Build, Abhängigkeiten per FetchContent (zlib, Brotli, stb_vorbis)
scripts/build.cmd      Build mit den VS 2022 Build Tools (Ninja, Release)
src/host/              ts2.exe: lädt game/ts2.bin, ersetzt Fremdes, eigenes D3D11
tools/hfpak/           hfpak: entschlüsselt/entpackt die CryPaks von Homefront
tools/re/              Python-Helfer für die EXE (Disassembler, Xrefs, Manifest)
re/ghidra_scripts/     Ghidra-Skripte (Headless-Export von Funktionen/Strings)
docs/                  Befunde, Formate, Roadmap
game/                  ts2.bin + ts2/ (aus der eigenen Installation)        (ignoriert)
reference/homefront/   Teilkopie von Homefront nur für die Analyse          (ignoriert)
re/ghidra_project/     Ghidra-Projekt der EXE                               (ignoriert)
re/out/                Ghidra-Exporte                                       (ignoriert)
```

## Bauen

Voraussetzungen: Visual Studio 2022 (oder Build Tools) mit C++, CMake ≥ 3.24,
Ninja, Git. Python 3 mit `pefile`, `capstone`, `numpy` für `tools/re`.

```bat
scripts\build.cmd
```

Ergebnis in `build\bin\`: `ts2.exe`, `hfpak.exe`.

## Einrichten und Spielen

```bat
build\bin\ts2.exe --einrichten "G:\SteamLibrary\steamapps\common\Homefront_The_Revolution"
build\bin\ts2.exe
```

Das Einrichten liest die eigene Homefront-Installation einmal und legt in
`game/` ab:

- `ts2.bin` (5,9 MB): nur der TS2-Teil der EXE und die Daten, die er anfasst;
  was genau, legt `src/host/ts2_manifest.inc` fest.
- `ts2/` (1,7 GB): die TS2-Daten aus `lsao_cached.pak` und `core_patch.pak`,
  mit hfpak entpackt, jede Datei gegen ihre CRC32 geprüft
  ([docs/formate/crypak.md](docs/formate/crypak.md)).

Danach braucht es Homefront nicht mehr. Einstellungen stehen in `ts2.ini`
neben der EXE (Auflösung, Waffenwechsel-Tempo, Sprung, Tasten, Freischaltungen, Ton), das Protokoll in
`ts2.log`.

## Quellen

- [split2-recomp_ff](https://github.com/frankyfife/split2-recomp_ff): Xbox-Recompile
  (archiviert), Adressen und Logik der Erweiterungen als Referenz
- [TS2Redux](https://github.com/HFTSRedux/TS2Redux): Mod für den Homefront-Port
  (Fehlerbehebungen, Freischaltungen; Pak-Formate im Wiki)
- [OpenRadical/symbol_maps](https://github.com/OpenRadical/symbol_maps): Symbolkarte
  der GameCube-Fassung von TS2
- [OpenRadical/tspak](https://github.com/OpenRadical/tspak): P4CK/P8CK-Paks von TS2
