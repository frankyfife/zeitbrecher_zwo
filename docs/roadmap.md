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
| Menütexte nicht in ihren Balken (Pausen-/Game-over-Menü, 3D) | behoben: Textausgabe 0x14121DA7C bekommt v umgerechnet (am Bild gemessen), Texte liegen auf den Balkenmitten; Maus folgt |
| Überlagerte Meldungen (Aufsammeln, Waffen) | vermutlich mit den Redux-Anzeigefixes behoben: sie ändern in der HUD-Meldungsliste `0x1411B08EC` Zeilenabstand (`lea ebx,[rsi+0x13B]` → `0x24B`) und Schrift; im Spiel noch zu bestätigen |
| Maus im Menü nach Rundenende (Pause 2. Ebene, Tod, Levelende) | behoben: Einträge der 3D-Menüs mit Breite 0 schieden aus; geprüft: Pause → Beenden → "Beenden?" → Beenden per Maus. Tod/Levelende nutzen dieselben 3D-Menüs, dort nicht eigens nachgestellt |
| Kistenschatten schwarz statt weich (Xbox: schwache Abdunklung) | behoben: Eckenfarbe des Flecks ist im PC-Port Unsinn (Floats statt Farbe), eigener Shader mit fester Deckkraft (`[grafik] schatten`), siehe host.md "Schatten" |
| Transparenz (Laserschranken in Siberia schwarz) | behoben: PC-Textur 2452 (und weitere Leuchttexturen) ohne Alpha, ts2.exe leitet das Alpha aus der Helligkeit ab (`[grafik] transparenz`), siehe host.md "Transparenz" |
| Überstrahlt in Siberia mit Windows-HDR | erste Abhilfe: eigene HDR-Ausgabe (`[grafik] hdr`, `hdr_weiss`) |
| Waffengeräusche anders als auf der Xbox | Proben gleich benannt (22 kHz); der PC-Port liest die Hall-Skripte `ts2/sound/reverb/levelNN_reverb.s` (Raum → Effektnummer) nie, die Xbox hallt je Raum. Nachbau: Raum verfolgen, Effektnummer → I3DL2-Voreinstellung der XBE, XAudio2-Hall |
| Launcher schwarz, Reiter, Titel "Zeitbrecher Zwo" | erledigt |
| Fehlerbehebungen aus TS2 Redux | im eigenen Code (`fixes.inc`, immer an), HD-Texturen unter `game/hd/`; eigene HD-Texturen (`tools/hd/make_hd.py`, docs/hd-texturen.md): Fadenkreuze, HUD-Bögen, Banner, Ladebild eigen, Kiste/Riffelblech/Zaun/Baum/Symbole noch von Redux |
| Chicago (Level 2) startet nicht (Absturz nach Überspringen) | behoben: TS2s Spielspeicher-Arena (58 MB, `0x1424E23D8`) lief mit den HD-Texturen über, `0x1411A9A10` rief exit(1); jetzt 192 MB. Geprüft: Chicago läuft mit HD-Texturen. Nebenbei: `ts2/data/cs_l_int.pak` fehlt im PC-Port (Xbox hat es) |
| Echte Schatten (Lampen, Sonne) | bewertet (docs/schatten-bewertung.md), vom Nutzer verworfen: keine Umsetzung |
| Level nach und nach freischalten wie auf der Xbox, Spielstand | erledigt: Xbox-Regeln aus der PAL-XBE, `profil.bin` (host.md "Fortschritt") |
| Untertitel fehlen | behoben: Zwischensequenz-Untertitel waren im PC-Port aus (nur bei nicht-englischer Sprache an), `[sprache] untertitel` |
| Hall für Umgebung und Waffen | erledigt: XAudio2-Hall nach Dambusters Raumtabellen, übrige Räume mit der häufigsten Voreinstellung (`[ton] hall`) |
| Redux-Issue #12/#25: Robot Factory hängt nach dem 2. Energieknoten (Fortschritt blockiert) | in ts2.exe nicht nachstellbar (Pfad wie Xbox, alle Varianten getestet); Watchdog (Launcher: Debug-Optionen) für den Fall, dass es doch hängt |
| Redux-Issue #3: Regen auf der Kamera (schwarze Quadrate, von Redux nur abgeschaltet) | behoben: Mischart 9 SUBTRACT → REV_SUBTRACT, Regen wieder an |
| Redux-Issue #21: Fotokamera-Bild weiß/verschmiert | Sucher in ts2.exe richtig (erzwungen geprüft); Weißbild nach dem Foto ungeprüft (Kamera erst spät in NeoTokyo) |
| Redux-Issue #4: Lichtflackern bei Mündungsfeuer/Feuer | behoben: Lichtschleifen der Shader mit richtigen Lagen und Dämpfung |
| Redux-Issue #23: Computeranzeigen abgeschnitten | behoben (Schriften der Bildschirm-Rückrufe auf Konsolengröße), mit Testzeichnung geprüft, echte Anzeige im Level noch nicht |
| Redux-Issue #20: Boss-Lebensbalken schlecht lesbar | behoben (Farbe halbiert wie Konsole 0x80 = voll) |
| Redux-Issue #5: Absturz in der Figurenauswahl | behoben: Texturverwaltung gibt wiederverwendete Texturen frei, Liste begrenzt; 1500 Figurenwechsel ohne Absturz |
| Schatten im ganzen Spiel schwarz (Chicago: Tische, Stühle, Kisten) | behoben: Eckenalpha der Schattenzeichnungen falsch (Licht zählt je Lampe 1 dazu), eigener Shader mit Textur-Alpha wie Xbox (`[grafik] schatten` = 0,62); Alpha-Texturen gegen das Xbox-Image verglichen (docs/alpha-texturen.md) |
| Rote Figuren/Waffe in Chicago | behoben: Raumhelligkeit aus der .raw wie Xbox (PC-Textleser lief über einen festen Puffer); betrifft auch Level 41 |
| Schriften unscharf: Buchstaben in höherer Auflösung nachzeichnen (Vektorquelle, gerendert in die Schrifttextur) | erledigt: SVG-Umrisse, 4fache Schrifttexturen, Umrechnung in ts2.exe (fontfix) |
| Cheats: unbegrenzte Munition, kein Cooldown; eigener Launcher-Reiter "Freischalten" | erledigt (nur Spieler, KI unverändert) |
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
5. Grafikfehler "schwarze Laserfenster": erledigt. Es sind die
   Laserschranken der Zellen in Siberia; ihre PC-Textur 2452 hat kein Alpha
   (Xbox: DXT3 mit Alpha). ts2.exe leitet für solche Leuchttexturen das Alpha
   aus der Helligkeit ab (`[grafik] transparenz`, host.md "Transparenz").
   Offen: andere Leuchttexturen der Liste (Mündungsfeuer u. a.) im Spiel
   ansehen, sobald sie mit SRC_ALPHA/INV_SRC_ALPHA gezeichnet werden; die
   Menüs nutzen auch Ziel-Alpha-Mischungen (7*8, 7*2) und 14*15, dort ist
   kein Fehler bekannt.
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
