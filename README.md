# zeitbrecher_zwo

Arbeitsrepository für einen eigenständigen PC-Port von **TimeSplitters 2**,
aufgebaut auf dem nativen x64-Port, den Dambuster Studios 2016 in
*Homefront: The Revolution* versteckt hat (`Homefront2_Release.exe`).

Ziel: Dekompilat und Funktionsbestimmung dieses Ports, daraus ein PC-Port mit
den Funktionen, die der Xbox-Recompile-Fork
([split2-recomp_ff](https://github.com/frankyfife/split2-recomp_ff)) dem Spiel
gegeben hat (Sprung, Rennen, Mauszielen, Freecam, Waffenhandling, Kamera/FOV),
dazu 5.1-Ton, native hohe Bildrate (≥ 120 fps), HDR und ein Launcher im
Retro-Futurismus-Stil.

Der Stand und die Reihenfolge stehen in [docs/roadmap.md](docs/roadmap.md).

## Rechtliches

Dieses Repository enthält **keine** Daten oder Code des Spiels. Man braucht
eine eigene Kopie von *Homefront: The Revolution* (Steam, App 223100). Nie
committen: `game/`, `reference/`, `re/ghidra_project/`, `re/out/` (alles in
`.gitignore`). Dekompilierter Code des Spiels gehört nicht in committete
Dateien; Adressen, Namen, Strukturbeschreibungen und eigener Code schon.
Der RSA-Schlüssel für die Paks wird zur Laufzeit aus der eigenen EXE gelesen.

## Aufbau

```text
CMakeLists.txt         Werkzeuge (und später der Port), Abhängigkeiten per FetchContent
scripts/build.cmd      Build mit den VS 2022 Build Tools (Ninja, Release)
tools/hfpak/           hfpak: entschlüsselt/entpackt die CryPaks von Homefront
tools/re/              Python-Helfer für die EXE (Disassembler, Xrefs, Strings)
re/ghidra_scripts/     Ghidra-Skripte (Headless-Export von Funktionen/Strings)
docs/                  Befunde, Formate, Roadmap
game/ts2/              entpackte TS2-Daten (aus lsao_cached.pak + core_patch.pak)   (ignoriert)
reference/homefront/   minimale Kopie von Homefront zum Vergleich/Analysieren     (ignoriert)
re/ghidra_project/     Ghidra-Projekt der EXE                                     (ignoriert)
re/out/                Ghidra-Exporte (functions.tsv, string_refs.tsv)            (ignoriert)
```

## Bauen

Voraussetzungen: Visual Studio 2022 (oder Build Tools) mit C++, CMake ≥ 3.24,
Ninja, Git. Python 3 mit `pefile`, `capstone`, `numpy` für `tools/re`.

```bat
scripts\build.cmd
```

Ergebnis: `build\tools\hfpak\hfpak.exe`.

## Spieldaten übernehmen

```bat
set HF=G:\SteamLibrary\steamapps\common\Homefront_The_Revolution
build\tools\hfpak\hfpak.exe extract --exe "%HF%\Bin64\Homefront2_Release.exe" "%HF%\gamehf2\lsao_cached.pak" game
build\tools\hfpak\hfpak.exe extract --exe "%HF%\Bin64\Homefront2_Release.exe" "%HF%\gamehf2\core_patch.pak" game ts2/
```

Die zweite Zeile legt die drei TS2-Shader aus dem Homefront-Patch über die
Originale (CryPak-Schichtung: der Patch gewinnt). Jede Datei wird gegen ihre
CRC32 geprüft. Format: [docs/formate/crypak.md](docs/formate/crypak.md).

Für die Analyse und spätere Vergleichsläufe liegt unter `reference/homefront/`
die Teilmenge von Homefront, die TS2 braucht (`Bin64`, `engine`, die Paks, die
der TS2-Redux-Ripper behält; etwa 2,5 GB). Die Steam-Installation bleibt
unverändert.

## Quellen

- [split2-recomp_ff](https://github.com/frankyfife/split2-recomp_ff): Xbox-Recompile
  (archiviert), Adressen und Logik der Erweiterungen als Referenz
- [TS2Redux](https://github.com/HFTSRedux/TS2Redux): Mod für den Homefront-Port
  (Fehlerbehebungen, Freischaltungen; Pak-Formate im Wiki)
- [OpenRadical/symbol_maps](https://github.com/OpenRadical/symbol_maps): Symbolkarte
  der GameCube-Fassung von TS2 (Funktionsnamen)
- [OpenRadical/tspak](https://github.com/OpenRadical/tspak): P4CK/P8CK-Paks von TS2
