# Befunde: Homefront2_Release.exe

Stand 2026-11-09. Adressen als VA (ImageBase `0x140000000`); "Offset" heißt
Dateioffset. Werkzeuge: `tools/re/hfdis.py`, `tools/re/xref.py`,
`tools/re/strs.py`, Ghidra-Projekt unter `re/ghidra_project/` (Export in
`re/out/`).

## Das Programm

| | |
| --- | --- |
| Datei | `Bin64/Homefront2_Release.exe`, 41 827 328 Byte, x64, MSVC |
| Build | 781467, `HF2_Tech_PC_(781467)_04_04_all`, 2017-02-13 (`buildinfo.txt` in `chunkmap.pak`) |
| Engine | CRYENGINE (Dambuster-Zweig, "Game03\GameDll"), Unity-Builds (`bintemp\uber_files\…`) |
| Abschnitte | `.text` 0x1000–0x1F79ACF, `PSFD00`, `.rdata` 0x1F7D000 (5 MB), `.data` 0x2479000 (28 MB virtuell), `.pdata` (≈ 123 000 Funktionen), `.rsrc`, `.reloc` |
| Pfade im Build | `d:\work\dsdb\Code\…` (568 `__FILE__`-Strings) |
| DLLs daneben | fmodex64/fmod_event64 (Ton), bink2w64 (Videos), steam_api64, Txaa, CUESDK, d3dcompiler_47 |

Es gibt keine PDB, aber `.pdata` liefert für jede Funktion Anfang und Ende,
und die `__FILE__`-Strings benennen viele Übersetzungseinheiten.

## TimeSplitters 2 in der EXE

TS2 ist **kein** eigenes Programm und keine DLL, sondern in die EXE gelinkt.
Das interne Projekt hieß **`Code\TS2\ts2redux`** (der Name der Fan-Mod ist ein
Zufall). Die Plattformschicht, die TS2 an die Engine bindet, ist an ihren
`__FILE__`-Strings zu erkennen:

| Datei | Zweck (vermutet) | Funktionen, die den Pfad nutzen |
| --- | --- | --- |
| `ts2redux\cryengine\ce_file.cpp` | Dateizugriff über CryPak (`ts2/…`) | 0x1412252AC … 0x1412256F8 |
| `ts2redux\cryengine\ce_music.cpp` | Musik (ogg) | 0x1412259F8 … 0x141225F20 |
| `ts2redux\cryengine\ce_sound.cpp` | Klang (wav), vermutlich über FMOD | 0x14122695C … 0x1412276A0 |
| `ts2redux\cryengine\ce_lan.cpp` | LAN/System Link | 0x1412284BC, 0x141228630 |
| `ts2redux\dx11\dlImmediate.cpp` | Immediate-Mode-Zeichnen (Ersatz für die Konsolen-Grafik) | 0x141228F30, 0x141229070 |
| `ts2redux\dx11\dx11.cpp` | D3D11-Renderer | 0x14122B214, 0x14122B458 |

Die Spiellogik von TS2 selbst hat (fast) keine `__FILE__`-Strings. Ihre
Daten liegen in `.rdata` grob ab Offset 0x21A0000 bis 0x21EA000, darunter die
Objekttabelle (`ob/neotokyo/…xbr`, `ob/planetx/…`, `ob/rfactory/…`,
`ob/csprops/…pob`) und die Animationsliste (`host0:anim/data/ts2/…raw`, ein
Überbleibsel der PS2-Fassung, Offset 0x21B9BD8). Der Code von TS2 ist ein
zusammenhängender Block, 0x141069B20 < f < 0x141231770 (4 544 Funktionen,
≈ 1,8 MB), mit genau einem direkten Einsprung von außen (Fabrik
`0x14122C2B0`, vtable `0x1421EAE20`); Einzelheiten in
[funktionen.md](funktionen.md).

## Wie Homefront TS2 startet: "Radical"

Auf der Homefront-Seite heißt TS2 **Radical** (nach Free Radical). Der
Automat im Spiel ist die Entität `Scripts/Entities/PlayerInteractive/radical.lua`;
sie ruft eine Lua-Bindung namens `Arcade`, eingerichtet in `0x1401626E8`:

| Lua | Handler | Was er tut |
| --- | --- | --- |
| `Arcade.StartRadical(entityId)` | 0x14015EDC0 → 0x14015EC48 | `g_pGame`(`*0x14262EAF8`)`+0x468` → `0x14015E820` (TS2 starten), `+0x478` → `0x14015E9B8` (Eingabe/Ansicht "radical_p1"), `+0x408` → HUD aus (`0x1403DBF6C`), Action-Map "radical" an (Priorität 1000) |
| `Arcade.StopRadical()` | 0x140160304 → 0x14016028C | das Gegenstück; danach Profilattribut `RadicalUnlocked` = 1 (`0x14015ED48`), wenn es noch nicht gesetzt war (`0x14015ED8C`) |
| `Arcade.RadicalActive()` | 0x140160330 | läuft TS2? |
| `Arcade.RadicalRendererInitialised()` | 0x1401603A0 | ist TS2s Renderer bereit? |

Dazu: CVar `s_RadicalVolume` ("Radical Arcade master volume", auch von
`ce_sound.cpp`-Code bei 0x141226070 gelesen), Menüknopf `RadicalButton`
(`0x140232118`, Extras-Menü, erscheint mit `RadicalUnlocked`), Netzwerk
`CRadicalNetwork` (RMI `SvRecvPacket`/`ClRecvPacket`/`…BroadcastPacket`: das
LAN-Spiel von TS2 läuft über CryEngines Netz). `isRadicalActive` bei
0x1403377F4.

Für einen Starter heißt das: TS2 läuft *innerhalb* einer laufenden
Homefront-Sitzung als eigener Modus. Direkt starten = nach dem Hochfahren
der Engine `0x14015EC48` (bzw. die Objekte bei `g_pGame+0x468/+0x478`)
aufrufen, ohne Level und Menü von Homefront.

## Pak-Zugriff

Die Paks und ihre Verschlüsselung: [formate/crypak.md](formate/crypak.md).
Relevante Funktionen:

| VA | Funktion |
| --- | --- |
| 0x140D4DD90 | Kommentar-Header einer Pak lesen und prüfen (Signatur, Schlüssel, QL-Trailer) |
| 0x140D2C6A0 | Verschlüsselungs-Header übernehmen (RSA/OAEP der IV und Schlüssel) |
| 0x140D31CF0 | Zentralverzeichnis lesen und entschlüsseln |
| 0x140D53B32 | Dateidaten entschlüsseln (Schlüssel- und IV-Wahl) |
| 0x140D2F4C0 | `DecryptBufferWithStreamCipher` (Twofish-CTR) |
| 0x140D31920 | `ZipDir::Cache::DecompressFile` (Deflate/Brotli) |

## Offen

- Wie TS2 gestartet wird (vom Homefront-Menü bzw. Automaten) und wo der
  Einsprung in TS2s Hauptschleife liegt. TS2Redux' `StraightIntoTS2` setzt
  dort an.
- Wie die Bildrate geregelt ist: TS2 zählt auf der Konsole Ticks pro
  vertikaler Austastlücke (Xbox: `FUN_001AE3C0`, siehe
  [xbox-referenz.md](xbox-referenz.md)); der PC-Port muss das ersetzt haben.
- Ob die Strukturen der Spielobjekte die Xbox-Offsets behalten haben (bis auf
  Zeiger, die auf x64 8 statt 4 Byte breit sind).
