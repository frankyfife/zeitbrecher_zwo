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
| Launcher | erste Fassung (Qt 6, `ts2launcher.exe`): alle Optionen, Tastenerfassung, Einrichten, Spielen, Sprache, Waffennamen (wie im Xbox-Fork); offen: Feinschliff |

## Spieldurchlauf 2026-11-10 (Befunde des Nutzers)

| Befund | Stand |
|---|---|
| "%d/%d Spieler" am Container (Siberia) | behoben: Xbox-Texttabelle hat ab 2425 einen Eintrag mehr |
| Menütexte nicht in ihren Balken (Pausen-/Game-over-Menü, 3D) | offen; mit TS2 Redux rücken Ring und Fußzeile an den Rand, der Versatz der Texte bleibt. Die Texte zeichnet `0x1410DAA74` in 3D: Punkt (−Breite/2, Eintrag +0x98) über `0x14121DA7C` mit der Matrix `*0x143F359D0`+0x60 transformiert, durch w geteilt und mit dem Bildausschnitt `*0x143F0F508` (Ursprung +0x00/+0x14, halbe Breite/Höhe +0x10/+0x24) abgebildet; die Balken gehen über die GPU-Projektion aus `camera_setup`. Gemessen liegen die Texte um den Faktor ≈0,8 enger als die Balken: Matrix oder Bildausschnitt passen nicht zur Projektion (Seitenverhältnis?) |
| Überlagerte Meldungen (Aufsammeln, Waffen) | offen; nachgestellt nur eine Meldung je Mal, Ursache noch nicht gefunden |
| Maus im Menü nach Rundenende | offen; nicht nachgestellt |
| Kistenschatten schwarz statt weich (Xbox: schwache Abdunklung) | behoben: Eckenfarbe des Flecks ist im PC-Port Unsinn (Floats statt Farbe), eigener Shader mit fester Deckkraft (`[grafik] schatten`), siehe host.md "Schatten" |
| Transparenz (Laserfenster schwarz, wenn aus) | offen, siehe Punkt 5; Werkzeug jetzt `TS2_DRAWDUMP` an der Stelle |
| Überstrahlt in Siberia mit Windows-HDR | erste Abhilfe: eigene HDR-Ausgabe (`[grafik] hdr`, `hdr_weiss`) |
| Waffengeräusche anders als auf der Xbox | Proben gleich benannt (22 kHz); der PC-Port liest die Hall-Skripte `ts2/sound/reverb/levelNN_reverb.s` (Raum → Effektnummer) nie, die Xbox hallt je Raum. Nachbau: Raum verfolgen, Effektnummer → I3DL2-Voreinstellung der XBE, XAudio2-Hall |
| Launcher schwarz, Reiter, Titel "Zeitbrecher Zwo" | erledigt |
| Fehlerbehebungen aus TS2 Redux | übernommen (`game/redux.bin`, HD-Texturen), siehe host.md |
| Absturz einmal im Siberia-Start (mit Redux) | einmal gesehen, in drei weiteren Läufen nicht |

## Nächste Schritte, konkret

1. 120 fps nativ, Stufe 2: Gegner und Objekte. Ein allgemeiner Ansatz pro
   Objekt verfälscht das Tempo (gemessen, [host.md](host.md) "Stufe 2");
   nötig ist der Umbau der Klassen-Aktualisierungen (Tabelle `0x142F94FF0`)
   Klasse für Klasse. Stufe 1 (Spieler und Kamera, `[grafik] takt=120`)
   ist erledigt.
2. Belichtung und Kontrast: Das Bild wirkt flau bzw. falsch belichtet.
   Vorhanden: Bildanpassung `[grafik] gamma/kontrast/helligkeit/saettigung`
   (eigener letzter Schritt, neutral aus). Ursache noch offen (der ungesetzte
   Sampler in Slot 1 ist es nicht, geprüft).
   Verdacht sRGB ausgeräumt: `TS2_BLENDPROBE=1` protokolliert auch die
   Texturformate; alle 724 Texturen im Glass-Smash-Start (Inhalte, eigene
   Ziele, 1920×1080-Kopien) sind `R8G8B8A8_UNORM`, keine sRGB-Form. TS2
   rechnet also im Gamma-Raum, die UNORM-Swapchain passt dazu. Womöglich fehlt
   eine Nachbearbeitung (Gamma, Tonemapping), die Homefront nach
   TS2s Render-Aufruf angewandt hat. Zuerst messen: Bild gegen die
   Homefront-Fassung und die Xbox-Fassung vergleichen.
3. HDR-Ausgabe: erste Fassung (scRGB, `[grafik] hdr`, `hdr_weiss`), am Gerät
   zu prüfen. Ursprünglich geplant: Swapchain mit HDR10 oder scRGB, TS2s
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
   Sampler dort (anisotrop, wiederholend) ändert kein Pixel; keine Ursache.
   Glass-Smash (Challenge) mit Debug-Schicht: keine Fehler; es fehlen keine
   Texturen. `TS2_BLENDPROBE=1`: TS2 legt Blend-Zustände mit Ziel-Alpha an
   (DEST_ALPHA/INV_DEST_ALPHA, DEST_ALPHA·ONE, wie auf der PS2) und löscht
   das Ziel nie selbst; im Flip-Modell ist dessen Alpha am Bildanfang also
   unbestimmt. Homefront übergab ein von CryEngine vorbereitetes Ziel.
   Nächster Schritt an der Glasstelle: `TS2_ALPHALOESCHEN=0` und `=1`
   (löscht das Ziel vor jedem Bild auf Alpha 0 bzw. 1) vergleichen; ändert
   sich das Glas, ist das die Ursache und die Lösung ein festes Löschen.
   Im Glass-Smash-Start ändern beide kein Pixel.
6. Freecam: erledigt (`[kamera] freecam`, Taste F6; WASD, Maus,
   Leertaste/C, Umschalt), geprüft im Glass-Smash-Start (über die Mauer
   geflogen, Level bleibt gezeichnet; zurück steht der Spieler unverändert).
   Auch bei 120 Hz geprüft (gleiche Strecke je Sekunde). Offen: von Hand
   im Spiel. Grundlagen: PC-Gegenstück zu Xbox
   `0x4B2CA0`: Globale ab `0x143F3A870` (Kamera-Zeiger), Modus
   `0x143F3A888`. Die Kamera-Aktualisierung `0x141081F28` verteilt nach Modus
   (2…9); Modus 8 (`0x1410828DB`) setzt die Kamera-Position (+0x2A0) aus
   `0x143F3A8A0` (x, y, z), die Blickrichtung (+0x310) aus Blickpunkt
   `0x143F3A8AC` minus Position, dazu `0x143F3A8B8/8BC` nach
   `[Kamera+0x208]+0x30/+0x34`, und den Raum (+0x2B8) aus dem aktuellen
   Spieler (`*0x143F1A388` +0xD78). Wie im Fork muss der Raum für die freie
   Position nachgeschlagen werden, sonst verschwindet das Level: PC
   `0x1411BFA08(&Kamera+0x2A0, 1.0f, [*0x142F5EAD8 +0xF4])` (Xbox `FUN_00120320(pos,
   1.0, player+0xE8)`), so in Modus 6 (`0x14107F8F0`, Flugkamera); liefert
   0, wenn kein Raum gefunden wird (dann den alten behalten). Modus 0 setzt `0x14107D6D4`
   (Kamera-Anlage). Sichtfeld erledigt (`[grafik] sichtfeld`,
   Kamera-Einrichtung `0x14107EE0C`).
7. Arcade und Challenge: per Skript geprüft (Arcade-Custom-Match `l_42_AR` mit
   Bots, Challenge „Glass Smash“ mit richtiger Uhr bei 60 und 120 Hz); offen
   nur ganze Durchgänge von Hand.
8. Lokalisierung: Texte erledigt. `tools/re/xbox_text.py` holt die fünf
   Tabellen der PAL-XBE nach `game/sprache/<code>.bin`, ts2.exe ersetzt
   damit die PC-Tabelle (`0x1424E2430`), geprüft auf Deutsch (Hauptmenü,
   Optionen, Umlaute). Der Launcher wählt die Sprache und holt die Texte
   selbst (gleiche Dateien wie das Skript). Offen: Sprachausgabe.
