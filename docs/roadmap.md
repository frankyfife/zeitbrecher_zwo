# Roadmap

Ziel: ein eigenständiger PC-Port von TimeSplitters 2 aus dem x64-Code des
Homefront-Ports, mit den Erweiterungen des Xbox-Forks, 5.1, nativ ≥ 120 fps,
HDR und einem eigenen Launcher.

## Der Weg

Ein vollständiges Dekompilat ist viel Arbeit (der TS2-Teil der EXE umfasst
grob geschätzt einige Tausend Funktionen). Deshalb in der bewährten Art von
re3/reVC: **erst am laufenden Original ansetzen, dann Funktion für Funktion
ersetzen.**

1. **Hook-Port.** Ein Starter lädt `Homefront2_Release.exe` aus
   `reference/homefront/` (oder der Steam-Installation), springt direkt in
   TS2 und lädt unsere DLL (`zb2.dll`). Die DLL hängt sich an einzelne
   TS2-Funktionen (MinHook o. ä.) und bringt sofort Funktionen: Rennen,
   Springen, Freecam, Waffenwechsel, Feuerrate, Sichtfeld, und die
   Fehlerbehebungen, die TS2Redux kennt (NeoTokyo-Absturz, Reaper-Absturz,
   Regen auf der Kamera, Absturz in der Charakterwahl).
2. **Dekompilieren durch Ersetzen.** Jede verstandene TS2-Funktion wird als
   eigener C++-Code in die DLL geschrieben und ersetzt das Original per Hook.
   Prüfbar ist jede Ersetzung einzeln gegen das Original.
3. **Eigene Plattformschicht.** `ce_file`, `ce_sound`, `ce_music`, `ce_lan`,
   `dx11` (siehe [exe-befunde.md](exe-befunde.md)) durch eigene Fassungen
   ersetzen: Dateien direkt aus `game/ts2/`, Ton über XAudio2 mit 5.1,
   eigener D3D11/D3D12-Renderer mit HDR10/scRGB, Logik in festen Ticks und
   interpolierte Darstellung für beliebige Bildraten.
4. **Eigenständig.** Sobald TS2s Code und die Plattformschicht vollständig
   eigen sind, braucht es die Homefront-EXE nicht mehr: eine eigene
   `zeitbrecher_zwo.exe` lädt nur noch `game/ts2/`.
5. **Launcher.** Eigenes Programm, optisch im Retro-Futurismus (Röhren,
   Neon, Chrom, Sternenhimmel; eigene Grafiken, keine fremden Logos oder
   Figuren). Einstellungen: Auflösung, Bildrate, HDR, Ton (Stereo/5.1),
   Steuerung, Erweiterungen.

## Stand

| Schritt | Stand |
| --- | --- |
| Paks entschlüsseln (`hfpak`) | erledigt, alle 9665 TS2-Dateien CRC-geprüft entpackt |
| TS2-Daten in `game/ts2/` | erledigt |
| Laufzeit-Referenz `reference/homefront/` | erledigt (2,5 GB) |
| TS2-Plattformschicht in der EXE gefunden | erledigt (`Code\TS2\ts2redux\…`) |
| Ghidra-Analyse und Export | erledigt (94 202 Funktionen, `re/out/`) |
| TS2-Funktionen von CRYENGINE trennen | erledigt: ein Block, 4 544 Funktionen, ≈ 1,8 MB ([funktionen.md](funktionen.md)) |
| Schnittstelle Homefront → TS2 | gefunden: Fabrik `0x14122C2B0`, vtable `0x1421EAE20`, Tick +0x18 |
| TS2-Funktionen benennen | offen (≈ 4 500 ohne Namen) |
| TS2-Einsprung finden (direkt starten) | teilweise: Lua `Arcade.StartRadical` → `0x14015EC48` ([exe-befunde.md](exe-befunde.md)) |
| Hook-DLL und Starter | offen |
| Erweiterungen (Tabelle in [xbox-referenz.md](xbox-referenz.md)) | offen |
| Plattformschicht ersetzen (Datei, Ton, Renderer) | offen |
| Launcher | offen |

## Nächste Schritte, konkret

1. TS2-Funktionen benennen und die vtable-Methoden des Laufzeitobjekts
   bestimmen ([funktionen.md](funktionen.md), "Nächste Schritte").
2. Den Einsprung in TS2 nutzen: Homefront ruft TS2 aus dem Menü
   (Extras → "TimeSplitters 2") bzw. vom Automaten auf. Danach ein Starter,
   der direkt dorthin geht.
3. Die Spielerstruktur im PC-Port finden (Blickwinkel, Bewegungsmodus,
   Haltung), ausgehend von den Xbox-Offsets; dann Rennen und Springen als
   erste Hooks.
4. P4CK-Paks unter `game/ts2/data/` lesen (eigenes Werkzeug), um Level und
   Objekte zu untersuchen.
