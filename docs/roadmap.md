# Roadmap

Ziel: ein eigenständiger PC-Port von TimeSplitters 2 aus dem x64-Code des
Homefront-Ports, mit den Erweiterungen des Xbox-Forks, 5.1, nativ ≥ 120 fps,
HDR und einem eigenen Launcher.

## Der Weg

TS2 ist im Homefront-Port eine abgegrenzte Bibliothek
([funktionen.md](funktionen.md)), die die Engine nur über `gEnv` und 31
Funktionen von außen erreicht. Daraus:

1. **Eigenständige EXE (erreicht).** `ts2.exe` lädt nur den TS2-Code aus der
   eigenen EXE (`game/ts2.bin`), ersetzt alles Fremde durch eigenen Code und
   gibt TS2 eigene Engine-Schnittstellen und ein eigenes D3D11-Gerät
   ([host.md](host.md)). Kein Homefront, keine CryEngine, kein Steam.
2. **Die Engine-Schnittstellen nachbauen** statt Attrappen: Eingabe, Ton
   (XAudio2, 5.1), Musik (stb_vorbis), LAN.
3. **Erweiterungen** direkt in `ts2.exe` (Sprung, Rennen, Freecam,
   Waffenhandling, Sichtfeld; Tabelle in [xbox-referenz.md](xbox-referenz.md)),
   Takt und Bild entkoppeln (≥ 120 fps), HDR.
4. **Dekompilieren durch Ersetzen.** TS2-Funktionen nach und nach als C++ in
   `ts2.exe` schreiben und ihre Bytes aus `ts2.bin` streichen, bis es leer
   ist. Jede Ersetzung ist einzeln gegen das Original prüfbar.
5. **Launcher**, optisch im Retro-Futurismus (Röhren, Neon, Chrom,
   Sternenhimmel; eigene Grafiken, keine fremden Logos oder Figuren).

## Stand

| Schritt | Stand |
| --- | --- |
| Paks entschlüsseln (`hfpak`) | erledigt, alle 9665 TS2-Dateien CRC-geprüft entpackt |
| TS2-Code in der EXE abgegrenzt | erledigt: ein Block, 4 544 Funktionen, ≈ 1,8 MB ([funktionen.md](funktionen.md)) |
| Schnittstelle zur Engine | erledigt: Fabrik, Start, Tick, Render samt Argumenten; `gEnv`-Felder ([host.md](host.md)) |
| TS2 ohne Homefront (`ts2.exe` + `game/ts2.bin`) | **erreicht**: spielbar bis ins Level (Chicago) |
| Spieler, Bewegung, Blick im PC-Code | gefunden (Spielertabelle, Dispatcher, Gehen, Fallen, Blick) |
| Erweiterungen | Render-Auflösung, Sprung (im Spiel geprüft), Waffenwechsel-Tempo (ungeprüft) |
| Eingabe | erledigt: Tastatur+Maus (Raw Input), Pad (XInput), Belegung in `ts2.ini` |
| Menü-Sperre der Homefront-Fassung ("Story/Quit") | erledigt: Arcade, Challenge, alle Story-Level, Options (`ts2.ini` [freischalten]) |
| Ton (5.1), Musik | offen (Attrappe bzw. Platzhalter) |
| Bildrate ≥ 120, HDR | offen |
| TS2-Funktionen benennen/dekompilieren | laufend (≈ 4 500 ohne Namen) |
| Launcher | offen |

## Nächste Schritte, konkret

1. Ton und Musik (XAudio2 mit 5.1, stb_vorbis).
2. Sprung mit Laufgeschwindigkeit, Rennen, Freecam, Sichtfeld.
3. Tick und Render entkoppeln, dann ≥ 120 fps und HDR.
4. Arcade und Challenge im Spiel prüfen (bisher nur die Menüs).
