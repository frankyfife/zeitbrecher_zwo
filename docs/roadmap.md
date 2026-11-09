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
| Erweiterungen (Xbox-Fork: Steuerung, Waffen, Menü) | Sprung, Rennen, Feuerrate, Waffenwechsel-Tempo, Nickgrenze, Menümaus |
| Eingabe | Tastatur+Maus (Raw Input, Belegung wie im Xbox-Fork), Pad (XInput), Maus im Menü |
| Menü-Sperre der Homefront-Fassung ("Story/Quit") | erledigt: Arcade, Challenge, alle Story-Level, Options (`ts2.ini` [freischalten]) |
| Ton (5.1), Musik | erledigt: XAudio2; eigener 5.1-Mixer auf TS2s Stimmen (gegen das Original geprüft), Ogg mit stb_vorbis |
| Bildrate ≥ 120, Belichtung, HDR | 60 Hz sauber getaktet; 120 Hz Stufe 1 (Spieler und Kamera nativ, Welt in 60-Hz-Takten, `[grafik] takt=120`); Belichtung/Kontrast fehlerhaft; HDR offen |
| TS2-Funktionen benennen/dekompilieren | laufend (≈ 4 500 ohne Namen) |
| Lokalisierung (Texte, ggf. Sprache) aus dem eigenen Xbox-Image, z. B. die PAL-Fassung (En/Fr/De/Es/It); Sprachwahl im Launcher | Texte erledigt (`[sprache] text=de`; Sprachwahl und Auslesen im Launcher, oder `tools/re/xbox_text.py`); offen: Sprachausgabe |
| Launcher | erste Fassung (Qt 6, `ts2launcher.exe`): alle Optionen, Tastenerfassung, Einrichten, Spielen; offen: Waffennamen, Feinschliff |

## Nächste Schritte, konkret

1. 120 fps nativ, Stufe 2: Gegner und Objekte. Ein allgemeiner Ansatz pro
   Objekt verfälscht das Tempo (gemessen, [host.md](host.md) "Stufe 2");
   nötig ist der Umbau der Klassen-Aktualisierungen (Tabelle `0x142F94FF0`)
   Klasse für Klasse. Stufe 1 (Spieler und Kamera, `[grafik] takt=120`)
   ist erledigt.
2. Belichtung und Kontrast: Das Bild wirkt flau bzw. falsch belichtet.
   Vorhanden: Bildanpassung `[grafik] gamma/kontrast/helligkeit/saettigung`
   (eigener letzter Schritt, neutral aus). Ursache noch offen (der ungesetzte
   Sampler in Slot 1 ist es nicht, geprüft); nächste Spur: Gamma-Kodierung
   der Swapchain bzw. eine Nachbearbeitung, die Homefront anwandte.
   Erster Verdacht: Die Swapchain ist `R8G8B8A8_UNORM` ohne sRGB, also eine
   mögliche Gamma-Abweichung zu dem, was TS2 schreibt. Außerdem fehlt
   womöglich eine Nachbearbeitung (Gamma, Tonemapping), die Homefront nach
   TS2s Render-Aufruf angewandt hat. Zuerst messen: Bild gegen die
   Homefront-Fassung und die Xbox-Fassung vergleichen.
3. HDR-Ausgabe (noch nicht drin): Swapchain mit HDR10 oder scRGB, TS2s
   Bild in einen erweiterten Bereich abbilden (Spitzlichter, Weißpunkt
   einstellbar), als Option im Launcher.
4. Vollbild: erledigt (randlos, Alt+Enter, `[grafik] vollbild`, DPI-bewusst);
   offen nur exklusives Vollbild, falls G-Sync es auf einem System braucht.
5. Grafikfehler: Glasflächen (z. B. die Fenster mit Laserschranken in
   Siberias Innenräumen) erscheinen schwarz statt durchsichtig. Vermutlich
   ein Blend- oder Alpha-Zustand, den der Render-Pfad ohne Homefront anders
   setzt; mit einer Aufnahme des Bildes und den D3D11-Zuständen prüfen.
   Werkzeug: `TS2_D3DDEBUG=1` (D3D11-Debug-Schicht, Meldungen ins Log). Im
   Siberia-Start meldet sie keinen Kopierfehler, aber dauerhaft einen
   ungesetzten Sampler in Slot 1 (2 Mio. Mal). Geprüft: ein eigener
   Sampler dort (anisotrop, wiederholend) ändert kein Pixel; keine Ursache. Nächster Schritt: an der
   Glasstelle mit der Debug-Schicht laufen lassen.
6. Freecam, Sichtfeld.
7. Arcade und Challenge: per Skript geprüft (Arcade-Custom-Match `l_42_AR` mit
   Bots, Challenge „Glass Smash“ mit richtiger Uhr bei 60 und 120 Hz); offen
   nur ganze Durchgänge von Hand.
8. Lokalisierung: Texte erledigt. `tools/re/xbox_text.py` holt die fünf
   Tabellen der PAL-XBE nach `game/sprache/<code>.bin`, ts2.exe ersetzt
   damit die PC-Tabelle (`0x1424E2430`), geprüft auf Deutsch (Hauptmenü,
   Optionen, Umlaute). Der Launcher wählt die Sprache und holt die Texte
   selbst (gleiche Dateien wie das Skript). Offen: Sprachausgabe.
