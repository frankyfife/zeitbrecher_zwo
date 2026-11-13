# Einrichten: vom Klon und der Steam-Installation zum spielbaren Stand

Diese Anleitung führt von einem frischen Klon dieses Repos und der eigenen
Steam-Installation von *Homefront: The Revolution* zu genau dem Stand, mit dem
entwickelt und getestet wird: dieselbe `ts2.exe` und ein `game/`-Ordner, der
Datei für Datei gleich ist (SHA-256 in [`scripts/pruefsummen.tsv`](../scripts/pruefsummen.tsv)).
Geprüft am 2026-11-13 mit Commit `c584f42` (Nachweis unten).

Kurz:

```bat
git clone https://github.com/frankyfife/zeitbrecher_zwo.git
cd zeitbrecher_zwo
python -m pip install -r requirements.txt
scripts\build.cmd
python -I scripts\einrichten.py --homefront "<Steam>\steamapps\common\Homefront_The_Revolution" ^
    --xbox "<Pfad>\TimeSplitters 2 (Europe) (En,Fr,De,Es,It).iso" [--privat <Ordner>]
build\bin\ts2.exe
```

Nichts unter `game/`, `reference/`, `re/out/`, `re/ghidra_project/` und
`privat/` gehört ins öffentliche Repo (alles in `.gitignore`).

## 1. Eingänge

| Eingang | Pflicht? | Quelle, Version | Prüfsumme (SHA-256) |
|---|---|---|---|
| Homefront: The Revolution | ja | Steam-App 223100, Build-ID 1733788 (`steamapps\appmanifest_223100.acf`), Spracheinstellung egal | siehe unten |
| `Bin64\Homefront2_Release.exe` | ja | 41 827 328 Byte, PE-Zeitstempel `58E252DC` | `c81fc8d6d61195a8f99d303926bcf32f6ca6638d8fa2deac9fc2d10c9ae4133c` |
| `gamehf2\lsao_cached.pak` | ja | 1 826 975 000 Byte | `21820e7dbe6c8d87992f54cd14018f91d7cbc58dd49551ac3a79d6da9648ad3f` |
| `gamehf2\core_patch.pak` | ja | 285 543 887 Byte | `77382de008db3eb0101241fb5b1f6e4ed7349f9bbcd0629d2b9f4dac73c0953b` |
| Xbox-Image TimeSplitters 2, PAL | empfohlen | eigenes Abbild der Disc „TimeSplitters 2 (Europe) (En,Fr,De,Es,It)“, Titel-ID ES-010, Region PAL; XISO oder Redump-Abbild | Inhalt (165 Dateien, siehe `xbox:inhalt`): `79e913088dccb7b1fafa65493fbb53bd10a24090c80087718cce6bf875236eb8`; `default.xbe`: `4695d91c038b70626ef0a62def0bffc1c46eb974cfea6040bb31cd2a60d3f641` |
| TS2-Redux-HD-Texturen | empfohlen | `ts2_redux.pak` aus [TS2Redux](https://github.com/HFTSRedux/TS2Redux), Datei `Install/Texture-Mods/ts2_redux.pak` auf Commit [`015685e9`](https://github.com/HFTSRedux/TS2Redux/tree/015685e95cfa2d360d8630bd1e5e1b340c961c40) (Datei zuletzt geändert in `60036e29`, 2021-11-05), 10 063 286 Byte, [Download](https://github.com/HFTSRedux/TS2Redux/raw/015685e95cfa2d360d8630bd1e5e1b340c961c40/Install/Texture-Mods/ts2_redux.pak); oder `privat/redux_hd/` | `111e74c85dec06f371fb4b6c2e81e72a9f9dc923ade3445fb9944b3903c68749` |
| eigener Schriftzug | nein | PNG des Nutzers, 1500×527 RGBA (`privat/schriftzug/neu_schriftzug_vorlage.png`), nicht im öffentlichen Repo | `c436cd7844c34e1ea61ddea44f8666874fb29a6f348ce40d53f5b9066de32c51` |

Das Xbox-Image wird nur gelesen. Geprüft wird sein **Inhalt** (alle Dateien
mit Größe und SHA-256), nicht die Image-Datei: ein volles Redump-Abbild
(mit Videopartition, `tools/re/xiso.py` findet die Spielpartition), ein XISO
und ein gekürztes XISO derselben Disc enthalten dieselben Dateien (geprüft
ist nur das XISO unten). Das hier verwendete Abbild ist ein XISO von
1 821 966 336 Byte (SHA-256
`08ba14c95cdb564e5104b19fa2bd4d69092e14024f908860b61f3342f807672c`), also
nicht die Redump-Datei selbst; deren Prüfsumme ist hier nicht erfasst.

Die TS2-Redux-Release-Seite bietet nur Installationsprogramme an; verwendet
wird ausschließlich die Datendatei `ts2_redux.pak` aus dem Repo (ein ZIP mit
SGI-Texturen), nichts davon wird ausgeführt. Im Redux-Repo gibt es eine
zweite, ältere Fassung unter `HD-Textures/ts2_redux.pak` (SHA-256
`d5b1573b…`); sie ist **nicht** die verwendete.

## 2. Werkzeuge (Versionen des geprüften Stands)

| Werkzeug | Version | Bemerkung |
|---|---|---|
| Windows | 11 Pro x64 (10.0.26200) | |
| Visual Studio 2022 Build Tools | 17.14.37, MSVC-Toolset 14.44.35207 (`cl` 19.44.35228) | Arbeitslast „Desktopentwicklung mit C++“; `scripts\build.cmd` nimmt Build Tools oder Community |
| Windows SDK | 10.0.26100.0 | |
| CMake | 4.4.3 (mindestens 3.24) | |
| Ninja | 1.13.2 | z. B. `winget install Ninja-build.Ninja` |
| Git | 2.55 | zum Klonen; FetchContent lädt Archive (Netz nötig) |
| Qt | 6.8.3, `msvc2022_64`, unter `C:\Qt\6.8.3\msvc2022_64` | nur für den Launcher; ohne Qt wird nur `ts2.exe`/`hfpak.exe` gebaut. Anderer Ort: `QT6_DIR` oder `CMAKE_PREFIX_PATH` |
| Python | 3.14.5 | Pakete gepinnt in [`requirements.txt`](../requirements.txt): numpy 2.4.6, Pillow 12.3.0, pefile 2024.8.26, capstone 5.0.9. Immer `python -I` |

Abhängigkeiten aus `CMakeLists.txt` (FetchContent): das GitHub-Quellarchiv
`https://github.com/<repo>/archive/<commit>.tar.gz` eines festen Commits,
von CMake gegen SHA-256 geprüft (ein anderes Archiv bricht den Build ab):

| Bibliothek | Version | Commit | SHA-256 des Archivs |
|---|---|---|---|
| [zlib](https://github.com/madler/zlib) | v1.3.1 | `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf` | `d9e270d46252734aa49770fbc544125391617956266f220bd63216c834f3a522` |
| [Brotli](https://github.com/google/brotli) | v1.1.0 | `ed738e842d2fbdf2d6459e39267a633c4a9b2f5d` | `aaa739962a45b508b2e783b915e6b2b57ed3b12bd4b0feac73acfb144dffa54f` |
| [stb](https://github.com/nothings/stb) (`stb_vorbis.c` v1.22) | 2026-10-19 | `a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d` | `ae498e10fadeba0c2e7aa86c71cc9989c4d895201cd26b8b72061c76cfde4edf` |

Mit diesen Archiven gebaut ist `ts2.exe` im Code gleich dem früheren Build
aus den Git-Klonen (Tags v1.3.1/v1.1.0).

Zur Laufzeit nutzt `ts2.exe` nur Windows-Bestandteile (D3D11,
`d3dcompiler_47.dll`, XAudio2).

## 3. Bauen

```bat
scripts\build.cmd
```

Ergebnis in `build\bin\`: `ts2.exe`, `hfpak.exe`, mit Qt `ts2launcher.exe`
samt Qt-DLLs (windeployqt). Ein Build aus demselben Quellordner ergibt
byteweise dieselben Programme bis auf Zeitstempel und PDB-Verweis. In einem
anderen Ordner gebaut, unterscheidet sich der Maschinencode an einigen
Stellen: MSVC leitet die Namen anonymer Namensräume und Lambdas aus dem
Quellpfad ab, das ändert die Reihenfolge einiger Funktionen. Das Verhalten ist
gleich (Nachweis unten).

## 4. Einrichten

```bat
python -I scripts\einrichten.py --homefront "<Steam>\steamapps\common\Homefront_The_Revolution" ^
    [--xbox "<TimeSplitters 2 (Europe) (En,Fr,De,Es,It).iso>"] ^
    [--privat <Ordner>] [--redux <ts2_redux.pak>] [--schriftzug <PNG>] ^
    [--spiel <game-Ordner>] [--bin <Ordner mit ts2.exe>] [--referenz]
python -I scripts\einrichten.py --pruefen        (nur vergleichen)
```

Das Skript arbeitet die Schritte unten der Reihe nach ab, prüft jeden
Eingang gegen `scripts/pruefsummen.tsv` (abweichende Eingänge brechen ab,
`--ohne-pruefung` macht daraus Hinweise) und vergleicht am Ende alle
Ergebnisse. Es kann beliebig oft laufen: was schon stimmt, bleibt.
Dauer am Entwicklungsrechner etwa 1,5 Minuten (bei kaltem Datei-Cache länger), davon das meiste Entpacken und Prüfsummen.

| Schritt | Werkzeug | Ergebnis in `game/` |
|---|---|---|
| 1. Homefront prüfen | Build-ID, SHA-256 | – |
| 2. TS2 aus Homefront | `ts2.exe --spiel <game> --einrichten <Homefront>` (ruft `hfpak.exe extract` für `lsao_cached.pak` und `core_patch.pak`; das Gleiche macht der Launcher unter „Spiel → Einrichten“) | `ts2.bin` (6 165 122 Byte, SHA-256 `8418b2cbeb351842c369af637a4102cd96ee61abcbfee0f7bfa6df40d663319a`), `ts2/` (9665 Dateien, 1,7 GB; Gesamtsumme `13497480…`) |
| 3. Redux-HD-Texturen | aus `privat/redux_hd/` kopiert, sonst `tools/re/hd_textures.py <ts2_redux.pak> <game>` (lädt das Pak von der festen URL, wenn kein `--redux`) | 37 Texturen in `hd/ts2/textures/` |
| 4. eigene HD-Texturen | `tools/hd/make_hd.py` (alle 37 Ziele nach `hd_eigen/`), dann `make_hd.py --installieren --nur "crosshair_*" "arc_*" ts2banner1b "ts2smallbanner*" ts2loading gmod_escort` (16 nach `hd/`, die ersetzten Redux-Dateien nach `hd_backup/`) | `hd_eigen/ts2/…`, `hd/…` |
| | `tools/hd/make_fonts.py`, `make_fonts.py --installieren` | `hd_eigen/schrift/*.svg`, `impact`/`book`/`booklight` in `hd_eigen/` und `hd/` |
| | `tools/hd/logo.py --export` (nur Ansicht, vom Spiel nicht gelesen) | `hd_eigen/logo/ts2newlogo_512x256.png` u. a. |
| 5. Schriftzug | `tools/hd/logo.py --installieren <PNG> --lage 112,40,1772,390 --relief 0.6 --menueschein` | `hd/…/misc/ts2newlogo.rgb` (2048×1024), `hd/…/misc/ts2glow_menue.rgba`, Kopie des PNG in `hd_eigen/logo/` |
| 6. Xbox-Image | `tools/re/xbox_text.py <iso> <game>` (wie „Spiel → Sprache → Texte holen“ im Launcher) | `sprache/{de,en,es,fr,it}.bin` |
| | `tools/re/xbox_alpha.py <iso> <game> --bin <game>/xbox_alpha.bin` | `xbox_alpha.bin` (18 Korrekturen, SHA-256 `6b6c612560010e211510912dc6dc695f07f1d8012c38db61a8dcc10ee2653b8e`) |
| 7. Vergleich | `scripts/pruefsummen.tsv` | – |

Die Reihenfolge von Schritt 6 zu 3–5 ist gleichgültig (geprüft: `xbox_alpha.bin`
ist vor und nach den Schriften gleich), Schritt 4 muss nach 3 kommen (die
Sicherung nach `hd_backup/`), der Logo-Export vor Schritt 5.

### Ohne die optionalen Eingänge

| fehlt | Folge |
|---|---|
| Xbox-Image | keine `sprache/`: Spiel, Menüs und Untertitel nur englisch (die Texte des PC-Ports); keine `xbox_alpha.bin`: 18 Leucht-/Funkentexturen ohne Xbox-Alpha (die allgemeine Transparenz-Abhilfe in `alphafix.cpp` bleibt) |
| Redux-Texturen (`--ohne-redux`, oder kein Netz und kein `--privat`) | `hd/` nur mit den eigenen Texturen (21 statt 42 Dateien): Kiste, Riffelblech, Zaun, Tanne und die Spielmodus-Symbole außer `gmod_escort` in Originalauflösung |
| Schriftzug | Hauptmenü und brennendes Logo mit dem Original-Schriftzug |

### Privater Ordner (`--privat`)

Was sich weder aus Homefront noch aus dem Xbox-Image erzeugen lässt, liegt
in einem eigenen Ordner (im Arbeitsbaum als `privat/`, von `.gitignore`
ausgeschlossen, oder in einem privaten Repo):

```text
privat/
  redux_hd/ts2/textures/…   die 37 Redux-Texturen, wie hd_textures.py sie nach game/hd legt (6,3 MB)
  schriftzug/neu_schriftzug_vorlage.png    Schriftzug des Nutzers (71 KB)
  re_out/                    Ghidra-Exporte (re/out, 14 MB), nur für die Analyse
```

Die Prüfsummen der 37 Redux-Dateien stehen in `pruefsummen.tsv`
(`eingang redux_hd`); sie sind byteweise gleich den Einträgen des Paks.

**Privates Duplikat.** Der volle Stand (alles aus diesem Repo plus
`privat/`) wird in einem zweiten, privaten Repo gesichert:
`python -I scripts/privat_sichern.py` (einmalig mit `--remote <URL>`). Es
nutzt einen eigenen Git-Ordner `.git-privat/` über demselben Arbeitsbaum;
die Historien sind getrennt, `privat/` kann so nicht ins öffentliche Repo
gelangen. Wiederherstellen: privates Repo klonen, dann wie oben bauen und
`einrichten.py … --privat privat` aufrufen.

### Was in `game/` sonst noch liegen kann

- `hd_backup/`: von `--installieren` ersetzte Dateien aus `hd/`, nur für
  `--zuruecksetzen`. Wer früher mehr eigene Texturen installiert und wieder
  zurückgesetzt hat, hat dort mehr Dateien (am Entwicklungsrechner 37 statt 16);
  das Spiel liest den Ordner nicht.
- `hd_eigen/logo/README.txt`, `hauptmenue_*.png`, `startbild_brennendes_logo.png`,
  `neu_schriftzug_3680x1080.png`: Notizen, Bildschirmfotos und ein
  Zwischenstand vom Arbeiten am Schriftzug; vom Spiel nicht gelesen, nicht
  Teil des Stands.
- `xemu/`, `xemu_ref/`: Vergleichsaufnahmen aus dem Emulator (`tools/xemu`), nicht Teil des Stands.
- Neben der EXE (nicht in `game/`): `ts2.ini` (Einstellungen; fehlende
  Schlüssel trägt `ts2.exe` mit Vorgaben nach), `profil.bin` (Spielstand),
  `ts2.log`.

## 5. Analyse-Umgebung (nur für die Arbeit an der EXE)

- `reference/homefront/`: Teilkopie der Installation (Bin64, Paks,
  Lokalisierung) für `tools/re` und Ghidra:
  `python -I scripts\einrichten.py --homefront … --referenz` (kopiert auch
  `privat/re_out` nach `re/out`, wenn es fehlt).
- `re/out/`: Ghidra-Exporte. Aus dem privaten Ordner, oder neu: Ghidra
  12.1.4 (PUBLIC, 2026-09-21), Projekt und Export in einem Lauf (etwa 10 Minuten
  Analyse mit den Standard-Analysatoren):

  ```bat
  "<ghidra>\support\analyzeHeadless.bat" re\ghidra_project hf2 ^
      -import reference\homefront\Bin64\Homefront2_Release.exe ^
      -scriptPath re\ghidra_scripts -postScript ExportFunctions.java "%CD%\re\out" ^
      -log re\out\ghidra.log > re\out\headless.txt
  python -I tools\re\classify.py re\out
  python -I tools\re\ts2deps.py > re\out\ts2_deps.txt
  ```

  Das Ghidra-Projekt (`re/ghidra_project`, 643 MB) ist genau dieser Lauf
  (zuletzt geändert beim Speichern nach dem Export, 2026-11-09 16:28); es
  enthält keine Handarbeit (Umbenennungen, Typen), die `re/out` nicht
  abdeckt. `re/out/ts2_interface.txt` stammt aus einem Einzelaufruf ohne
  eigenes Werkzeug im Repo; deshalb gehört `re/out` in den privaten Ordner.
  Ob eine neue Analyse byteweise dieselben TSV-Dateien ergibt, ist nicht
  geprüft.
- `src/host/ts2_manifest.inc` ist eingecheckt (erzeugt von
  `tools/re/make_manifest.py` aus der EXE); zum Einrichten nicht nötig.

## 6. Nachweis (2026-11-13, Commit `c584f42` plus diese Anleitung)

1. Frischer Klon in einen leeren Ordner, `scripts\build.cmd` (FetchContent
   aus den Archiven): baut ohne Fehler und Warnungen, mit Launcher.
   `ts2.exe` und `hfpak.exe` aus demselben Quellordner wie der
   Entwicklungs-Build sind bis auf Zeitstempel/PDB-Verweis byteweise gleich
   `build\bin\`; aus dem Klon-Ordner ist `hfpak.exe` im Code gleich, bei
   `ts2.exe` liegen einige Lambdas anders (4241 Byte in `.text`, Abschnitt 3).
2. `einrichten.py --homefront … --xbox … --privat … --referenz` in einen neuen
   `game`-Ordner: alle Einträge von `pruefsummen.tsv` gleich. Gegen das
   `game/` des Entwicklungsrechners (ohne `xemu*`): alle 9764 Dateien
   byteweise gleich (`ts2.bin`, 9665 unter `ts2/`, 42 unter `hd/`, 50 unter
   `hd_eigen/`, 5 unter `sprache/`, `xbox_alpha.bin`); dort zusätzlich nur
   die fünf Hilfsdateien in `hd_eigen/logo/` (Abschnitt 4). `hd_backup/`:
   16 Dateien, gleich den entsprechenden dort (am Entwicklungsrechner 37).
   `reference/homefront` (49 Dateien + `steam_appid.txt`) und `re/out` gleich.
   Ein zweiter Lauf ändert nichts; ohne `--privat` lädt er das Redux-Pak
   von der festen URL (Prüfsumme stimmt), ohne `--xbox` und Schriftzug
   meldet er, was fehlt.
3. Gleiches Bild: Siberia-Skript (`TS2_TAKT=60`, Kopie der Nutzer-ts2.ini mit
   `vollbild=0`, `[ton] an=0`, 4500 Takte, Bilder alle 600 Takte) mit dem
   Klon-Build und dem neuen `game` gegen `build\bin\ts2.exe` mit dem
   bisherigen `game`: alle acht Bilder (Zwischensequenz mit deutschen
   Untertiteln, Menü, Siberia-Tunnel) pixelgleich.

   Achtung beim Wiederholen: Mit dieser ini (Supersampling 2, HDR, Pad,
   Mehrspieler an) sind zwei Läufe **derselben** EXE mit demselben `game`
   nicht immer gleich (Zwischensequenzen und Level um einige Bilder
   versetzt, Takt 600 und 1800 immer gleich); ein Paar gleicher Läufe
   zeigt die Gleichheit, ein ungleiches Paar beweist nichts.
