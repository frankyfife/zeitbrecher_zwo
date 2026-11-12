# ts2.exe: TimeSplitters 2 ohne Homefront

Stand 2026-11-09: `ts2.exe` startet TS2 in einem eigenen Fenster mit
eigenem D3D11-Gerät, ohne Homefront, CryEngine oder Steam, und ist
spielbar: Menüs, Intro, Briefing und das Level Chicago laufen mit Tastatur
und Maus oder einem Pad; der Sprung funktioniert. Arcade, Challenge, alle
Story-Level und das Optionsmenü sind freigeschaltet. Ton, Musik und Sprache
laufen, auf 5.1-Geräten mit positionierten Geräuschen im Raum.

## Einrichten und Starten

```bat
build\bin\ts2.exe --einrichten "G:\SteamLibrary\steamapps\common\Homefront_The_Revolution"
build\bin\ts2.exe
```

Das Einrichten liest die eigene Homefront-Installation **einmal** und legt
in `game/` ab:

| Datei | Inhalt | Größe |
| --- | --- | --- |
| `game/ts2.bin` | der TS2-Code und die Daten, die er anfasst, aus `Homefront2_Release.exe` | 5,9 MB |
| `game/ts2/…` | die TS2-Daten aus `lsao_cached.pak` + `core_patch.pak` (hfpak) | 1,7 GB |

Danach wird Homefront nicht mehr gebraucht. `game/` gehört nie ins Repo.

`ts2.exe [--spiel <game>] [--takte N [--bild datei.bmp]]`: `--takte` hält
nach N Takten an (Tests), `--bild` speichert den letzten Bildpuffer.
Einstellungen in `ts2.ini` neben der EXE. Alle Optionen stehen mit Vorgabe und Kommentar in `src/host/inifile.cpp`; beim Start trägt `ts2.exe` fehlende Schlüssel und Abschnitte nach (vorhandene Werte und Kommentare bleiben):
Render-Auflösung, Vollbild, Waffenwechsel-Tempo, Sprung, Tasten, Freischaltungen, Ton. Protokoll:
`ts2.log` neben der EXE.

## Wie es funktioniert

### Was aus der EXE übernommen wird

`src/host/ts2_manifest.inc` (erzeugt von `tools/re/make_manifest.py`, nur
Adressen) legt fest:

| | Umfang |
| --- | --- |
| Code | der TS2-Block 0x141069B90–0x141231766 (1,8 MB) und `__chkstk` (0x141C50CA0) |
| Daten | `.data` vollständig (1,3 MB Rohdaten, Rest BSS genullt), aus `.rdata` die Seiten, die der Code anfasst, mit Lücken bis 256 KB (Tabellen) |
| PE-Kopf | eine Seite: Windows-Funktionen bekommen die Modulbasis als `HINSTANCE` |
| Importe | 31 Slots: C-Laufzeit (`msvcr100`) und ein paar Kernel32/User32/Advapi32-Funktionen |
| Fremde Funktionen | 31 Adressen außerhalb des Blocks, die der Code direkt ruft |

`ts2.exe` reserviert den Adressraum der Original-EXE ab `0x140000000`
(sodass alle Adressen in `docs/` gelten), belegt aber nur die übernommenen
Seiten. Ein Sprung in nicht übernommenen Engine-Code endet sofort in einer
Zugriffsverletzung; `ts2.log` nennt dann die Original-Adresse.

### Ersatz für alles Fremde (`src/host/replace.cpp`)

| Original (VA) | Was | Ersatz |
| --- | --- | --- |
| 0x140C6F8B0 / 0x140C6E840, Hüllen 0x140001D00 / 0x140001D10 | CryEngines Speicherverwaltung (`new`/`delete`) | `_aligned_malloc`/`_aligned_free` |
| 0x1400012D0, 0x1400012F0, 0x140001300, 0x140001EC0 | Critical Sections (Hüllen um Kernel32) | Kernel32 direkt |
| 0x1400015F0 | `ReleaseSemaphore(*h, 1, 0)` | dasselbe |
| 0x1408761DC, 0x14095E660 | leer (ausgebaute Protokollierung) | leer |
| 0x14088251C / 0x1402E5830 | `return 0` / `return 4` | dasselbe |
| 0x140004370 | `CryString::assign`, nur im CryPak-Pfad | Meldung (wird nicht gebraucht) |
| 0x141C34420, 0x141C33B50, 0x141C32C80, 0x141C33A80, 0x141C33FF0, 0x141C32B60 | libvorbisfile (`ov_open_callbacks`, `ov_info`, `ov_pcm_total`, `ov_time_seek`, `ov_read`, `ov_clear`), statisch gelinkt | stb_vorbis (`vorbis.cpp`) |
| 0x141C50C6E … 0x141C50ED6 | CRT-Sprünge: memcpy, memset, memcmp, powf, sinf, cosf, tanf, asinf, acosf, atanf, atan2f | eigene CRT |
| Importe `msvcr100!…` | sprintf, strncpy, fopen, fread, … `__iob_func` | eigene CRT; `__iob_func` liefert ein Platzhalterfeld, das die Datei-Funktionen auf stdin/stdout/stderr abbilden |

### Engine-Schnittstellen (`gEnv`, `src/host/fake.cpp`)

TS2 erreicht die Engine nur über CryEngines `gEnv` (statische Struktur bei
`0x1424790C0`). `ts2.exe` füllt die Felder, die TS2 liest, mit
**Attrappen**: Objekte, deren Methoden ihren ersten Aufruf in `ts2.log`
melden und 0 liefern. Was TS2 bisher verlangt:

| Feld | Bedeutung (Befund) | Methoden, die TS2 ruft | Stand |
| --- | --- | --- | --- |
| +0x60 | CryPak | — | **bleibt null**: TS2s `ce_file` (`sceOpen` 0x1412252AC …) fällt dann selbst auf `fopen("ts2/…")` zurück; das Arbeitsverzeichnis ist `game/` |
| +0xC0 | ? | +0xB0 (in Start) | Attrappe |
| +0xD0 | Soundsystem | +0x100(Mixer) → Strom-Objekt (+0x08 freigeben, +0x10 Abtastrate, +0x18 drei Floats) | **echt**: XAudio2 (`audio.cpp`), siehe "Ton" |
| +0xD8 | ? | +0x4D8 (je Takt) | Attrappe |
| +0x140 | Profiler | +0x90/+0x98 (Beginn/Ende "TS Tick"/"TS Render") | Attrappe (passt so) |
| +0xF8, +0x128, +0x168 | | bisher keine | Attrappe |

### Ablauf

1. `obj = Fabrik 0x14122C2B0(gEnv, &semaphore)`
2. `obj->Start(&DeviceHolder)` (vtable +0x08); der Halter ist eine Struktur,
   deren Feld 0 das `ID3D11Device` ist. TS2 legt einen D24S8-Tiefenpuffer an,
   lädt Shader, das Titelbild-Level `arcade/l_103.pak`, `fe_anim.pak`,
   Texturen.
3. Je Bild: `obj->Tick(&DeviceHolder)` (+0x18), dann
   `obj->Render(&DeviceHolder, &ContextHolder, rtv, backbuffer)` (+0x20),
   dann `Present`. `rtv` ist das Render-Target-View des Bildpuffers, das TS2
   mit `OMSetRenderTargets` setzt; `backbuffer` die Textur, aus der es mit
   `CopySubresourceRegion` für Bildschirmeffekte kopiert.

Die Render-Auflösung ist im Original fest 1920×1080 (`0x141069E0B`/`E10`,
Aufruf des DX11-Setups `0x14122B78C`); `ts2.exe` setzt dort die Werte aus
`ts2.ini` ein.

Hinweis zu Offsets: `ID3D11DeviceContext` erbt vier Methoden von
`ID3D11DeviceChild`; die Methodennummern liegen daher vier Plätze hinter einer
naiven Zählung (`OMSetRenderTargets` = +0x108, `RSSetViewports` = +0x160,
`CopySubresourceRegion` = +0x170, `ClearRenderTargetView` = +0x190).

## Eingabe (`src/host/input.cpp`)

TS2 fragt keine Geräte ab. Im Homefront-Port übersetzte Homefront seine
Action-Map `radical` (`0x14015F880`, Aktionen `rad_*`) und reichte das
Ergebnis über Methoden des TS2-Laufzeitobjekts weiter; TS2s
`scePadRead(port, PadData*)` (`0x141225898`) baut daraus die Pad-Daten.
`ts2.exe` ruft dieselben Methoden:

| vtable | Bedeutung | in TS2 |
| --- | --- | --- |
| +0x50 (int) | Modus: 1 Tastatur+Maus (Port 6), 0 Pad (Port 0) | `0x143F0F4A0` |
| +0x58 / +0x60 | Pad verbunden an/aus (ohne: leere Pad-Daten) | `0x143F0F494` |
| +0x68 / +0x70 (float) | linker Stick X (rechts +) / Y (vorwärts +) | Pad +0x30 / −(+0x38) |
| +0x78 / +0x80 (float) | Pad: rechter Stick X/Y; Mausmodus: Mausbewegung, addiert (Y negiert übergeben) | Pad +0x34 / +0x3C; Maus × 0,2 (`0x14259CF44`) |
| +0x88 (int) | Tastenbits | Pad +0x24 (jetzt), +0x20 (vorher), +0x28 gedrückt, +0x2C losgelassen |

Tastenbits (aus `0x14015F880`):

| Bit | Aktion | Bit | Aktion |
| --- | --- | --- | --- |
| 0x1 | L2 | 0x1000 | hoch (Steuerkreuz/Menü) |
| 0x2 | R2 | 0x2000 | rechts |
| 0x4 | L1 | 0x4000 | runter |
| 0x8 | R1 | 0x8000 | links |
| 0x10 | Dreieck | 0x10000 | Feuer (nur Port 6) |
| 0x20 | Kreis / Menü zurück | 0x20000 | Alt-Feuer |
| 0x40 | Kreuz / Menü OK | 0x40000 | Benutzen |
| 0x80 | Quadrat | 0x80000 | Nachladen |
| 0x100 | Start / Pause | 0x100000 | Ducken |
| | | 0x200000 | Zoom |
| | | 0x400000 / 0x800000 | nächste / vorige Waffe |
| | | 0x1000000 / 0x2000000 | Zoom rein / raus |

Vorgaben wie die PC-Belegung des Xbox-Forks (`launcher/src/bindings.cpp`,
TS2s Belegung "Standard"); die Bits sind gegen Homefronts Übersetzer
geprüft (`rad_fire` 0x10000, `rad_fire_alt` 0x20000, `rad_zoom` 0x200000 …):

- **Bewegen und Zielen:** WASD; die Maus zielt (Raw Input, eingefangen,
  solange das Fenster vorn ist).
- **Maustasten:** links Feuer, rechts Zielen/Zoom, Mitte oder F
  Zweitfunktion.
- **Waffe:** Mausrad nach oben oder Q die vorige, nach unten die nächste.
- **Weitere Tasten:** E benutzen, R nachladen, Strg oder C ducken, X
  Quadrat, Tab Dreieck, Esc Pause.
- **Menü:** Enter/Rücktaste OK/zurück, Pfeiltasten.
- **Sprung (ts2.exe):** Leertaste.

Je Aktion bis zu drei Tasten (`ducken=162,67`). Ein `[tasten]`-Abschnitt
einer älteren Fassung (ohne `belegung=2`) wird einmal auf diese Belegung
umgestellt; die Lauftasten bleiben dabei, wie sie waren. Alles in `ts2.ini`
([tasten], [maus]) änderbar. Es gilt das zuletzt benutzte Gerät.

**Mausrad:** solange gezielt wird (Zielen-Taste gehalten, Vorgabe rechte
Maustaste), wählt es die Zoomstufen (hoch = näher), sonst wie bisher die
Waffe. Skript-Bits `04000000`/`02000000` = eine Raste hoch/runter. Geprüft:
Siberia, Scharfschützengewehr: zwei Rasten beim Zielen zoomen zweimal näher,
ohne Zielen wechselt die Waffe.

**Pad (XInput, `[pad]`).** TS2 liest im Spiel feste Steuerungs-Masken im
Spieler (+0x20, gefüllt von `0x1411DCBAC`): Feuer 0x10000, Alt-Feuer 0x20000,
Benutzen 0x40000, Nachladen 0x80000, Zoom 0x200000, Ducken 0x100000, Zoom
rein/raus 0x1000000/0x2000000, Waffe vorige/nächste 0x800000/0x400000. Das gilt
auch im Pad-Modus (Port 0); die Konsolen-Bits (L1, R1, Dreieck …) bewegen im
Spiel nichts und dienen nur den Menüs. Deshalb gingen am Pad nur Bewegung,
Blick und Menüs. Das Pad gibt jetzt dieselben Bits wie Tastatur und Maus, die
Sticks über Port 0 (TS2s eigenes Pad-Zielen, Nickgrenze des Spiels).

Vorgabe wie TimeSplitters 2 auf der Xbox: linker Stick gehen, rechter Stick
blicken, RT Feuer, LT Zweitfunktion, R3 Zielen (schaltet um,
`zielen_umschalten`), L3 Ducken, A Benutzen/OK, B zurück, Y Nachladen, X
Quadrat, Steuerkreuz links/rechts Waffe, hoch/runter Zoomstufen (und Menü),
Start Pause, Back Dreieck (Ziele). Dazu wie im Fork LB Rennen, RB Springen,
L3+R3 freie Kamera. Jede Aktion ist in `[pad]` umbelegbar (`feuer=RT`,
`freecam=L3+R3`, zwei Belegungen mit Komma). Knöpfe eines Akkords gehen erst
nach 4 Takten einzeln an TS2, damit L3+R3 weder duckt noch zielt. Weitere
Optionen: `an`, `vibration`, `vibration_staerke`, `totzone`,
`empfindlichkeit`, `umkehren`. Ohne Fensterfokus zählt das Pad nicht.

**Rütteln:** `0x141144DBC(pad, edx, r8d)` setzt die Motorbytes Pad +0x45/+0x44
(0..127, wie scePadSetActDirect auf der PS2). Schüsse setzen +0x44, Treffer
+0x45; TS2 lässt beide selbst abklingen. `features_tick` liest sie je Takt
über die Getter `0x141144E60`/`0x141144E24` für das Pad des Spielers
(`**0x143F1A388`); `input.cpp` gibt +0x44 auf den rechten und +0x45 auf den
linken Motor (`XInputSetState`), nur im Pad-Modus.

**Freie Kamera am Pad:** L3+R3; linker Stick fliegen, rechter Stick blicken,
Steuerkreuz hoch/runter oder RT/LT steigen/sinken, LB/RB schneller.

**Testskript:** `TS2_PADSKRIPT="takt:knoepfe,lx,ly,rx,ry,lt,rt[,ts2bits];…"`
ersetzt XInput (Zustand gilt bis zum nächsten Eintrag). `TS2_PADSPUR=1`
protokolliert die Motoren. Geprüft per Skript: Menüs nur mit dem Pad bis
Siberia; Feuer, Nachladen, Zielfernrohr mit drei Zoomstufen, Ducken, Sprung,
Rennen, Waffenwechsel, freie Kamera; Rütteln beim Schuss 127 → 0 in 5 Takten;
Tastatur und Maus pixelgleich zum Stand davor.

Testskripte ohne Bildschirm: `--eingabe takt:bits:anzahl,...` (Bits
hexadezimal; zusätzlich `80000000` Sprung, `40000000` vorwärts) mit
`--takte` und `--bild`. Geprüft: 320:40:5 wählt im Titel "Story", weitere
OKs überspringen das Intro und führen über Levelwahl und Briefing nach
Chicago; Sprung 0 → Modus 2 (+0,145) → Landung Modus 0.

## Freischalten (`src/host/menu.cpp`)

Die Homefront-Fassung zeigt nur "Story" und "Quit" und in der Levelwahl nur
Siberia und Chicago. Das ganze Spiel ist enthalten; Dambuster hat es hinter
Konsolen-Codes versteckt (TS2Redux, `UnlockCodes.md`). Die Codes stehen in
einer Tabelle (`0x1425078B8`) und schalten nur Flags um
([funktionen.md](funktionen.md), "Daten"): `0x142F2C360[i]` mit 0 alle
Story-Level, 1 Arcade, 2 Challenge, 4 Cheats, 5 Unverwundbarkeit, dazu
`0x142F2C35C` "alles". Das Hauptmenü (`0x1410C81B8`) zeigt Arcade und
Challenge nur, wenn ihr Flag gesetzt ist; "Options" fehlt darin ganz, die
Seite (`0x1424F1D78`) ist aber vollständig.

`ts2.exe` setzt die Flags vor dem Start laut `ts2.ini` [freischalten]
(Vorgabe: alles aus, also Fortschritt wie auf der Xbox, siehe
"Fortschritt"; die Schalter wirken wie die Konsolen-Codes) und trägt
immer einen eigenen Aufbau des Hauptmenüs in die Seitenbeschreibung ein
(`0x1424F2DB0`, wie TS2Redux): Story, Arcade, Challenge (wie auf der Xbox
immer sichtbar), Options (mit `optionen=1`), Quit, mit den Maßen und
Texten des Originals.

Geprüft per Skript: Arcade (League/Custom), Challenge (Challenge-Sets),
Options (Spieler, Ton, Abspann, Zwischensequenzen, Cheats) öffnen sich,
"Zurück" führt ins Hauptmenü; die Story-Levelwahl zeigt mehr als zwei
Level.

## Cheats (`src/host/cheats.cpp`)

`ts2.ini` [cheats] `munition`, `kein_cooldown` (Vorgabe 0; Launcher: Reiter
„Freischalten“, Gruppe „Cheats“, dort auch Unverwundbar und Alle
Cheats/Extras aus [freischalten]). Beide gelten nur für die Spieler (Figuren
der Spielertabelle); Waffentabelle und KI bleiben unverändert.

TS2s eigene Cheats (Optionen → Cheats, Tabelle `0x142507A58`, 13 Einträge)
schaltet Bit i von `0x143F2F998`; wirksam nur, wenn der Cheat frei ist
(`0x14111164C`) und die Spielart ihn erlaubt (`0x143F2F99C`). „Infinite
Ammo“ (Bit 4) prüft allein `0x141126A80` vor dem Verbrauch, für jeden
Schützen. Der eigene Cheat nutzt ihn deshalb nicht.

**Munition:** Der Verbrauch `0x1411213D8(Figur, Hand, Art, Menge, Art2)`
(Vorrat Figur +0x10A8 + 4·Art, Magazin Hand +0x34 + 4·Art; Aufrufe
`0x141126B11`, `0x141121471`) wird für Spieler übersprungen.

**Kein Cooldown:** vor jedem Takt, je Hand (Figur +0xE0/+0x680):
- Schussabstand: Der Handautomat `0x14112D4A0` feuert, wenn die Takte seit
  dem Schuss (+0xB8/+0xBC je Modus) den Abstand des Modus (+0x10 neu
  gedrückt, +0x14 gehalten) erreichen; sie stehen mindestens auf dem größten
  Abstand der Waffe: ein Schuss je Takt.
- Nachladen: Das Magazin wird sofort aus dem Vorrat aufgefüllt (wie
  `0x141121A70` am Ende des Nachladens, auch gleich nach jedem Schuss); es
  wird nie leer, TS2 beginnt kein Nachladen (`0x141129D94` → `0x141125634`,
  Zustand 7). Waffen ohne Magazin (Größe 0/1) bleiben wie sie sind.
- Waffenwechsel: Die Waffe (Spieler +0x1E8/+0x7B0; +0x34 Zustand, +0x38
  Fortschritt) springt beim Senken (2) und Heben (6) ans Ende: 4 Takte
  statt 2 × 0,35 s.

`TS2_WAFFENPROBE=1` protokolliert Schüsse, Magazin, Vorrat und Zustand der
Waffe von Spieler 1. Geprüft (Siberia, Scharfschützengewehr): Magazin und
Vorrat bleiben bei 5/5 über 30 Schüsse; ohne Munitions-Cheat 10 Schüsse in 10
Takten, Magazin voll bis der Vorrat leer ist, kein Nachladen; Wechsel in 4
statt 32 Takten; beide aus: Bild gleich wie vorher.

## Brennendes Logo (`src/host/logofix.cpp`)

Der erste Bildschirm (0x1410B5800, Xbox PAL 0x88CB0, gleiche Logik) zeichnet
400 Feuerteilchen (Klasse 1, misc/fireb1..8, Mischart 3) nur in den
Alphakanal (0x14122A6A0: gesetzte Bytes sperren Kanäle), dann misc/ts2glow
mit DEST_ALPHA/ONE nur in RGB: sichtbar ist Schein × Ziel-Alpha. Auf dem PC
leuchtete ein Drittel der Fläche mit halber Helligkeit:
- Teilchengröße (0x14112F3C0: (Alter·[+0x3C]+[+0x38])·Kamera+0x27C/z,
  höchstens [+0x40]) zählt in Bildpunkten der 640×480-Konsolensicht; der
  Port rechnet nur die Lage auf 1920×1080 um. Teilchen 2,25-mal zu klein.
- Xbox Art 7: ALPHAREF 0x7E (GREATER) statt 0,33; ihre Art 0 lässt den
  Alpha-Test an, auch für den Schein (PC: aus).

ts2.exe lenkt die Aufrufe 0x1410B586F (Teilchen) und 0x1410B58EF (Schein)
um: alphaRef 127/255 (0x14259D554), danach zurück; die Größe richtet
spritefix.cpp für alle Teilchen (siehe unten). TS2_LOGOFIX=0 aus; TS2_LOGOPROBE=<n> schreibt beim n-ten
Bild Ziel-Alpha und Farbe vor/nach dem Schein. Gemessen (Takte 100–200):
leuchtende Fläche 2,0 % → 4,6 % (Xbox 5,6 %), Rot-Median 36 → 67 (Xbox 65).
Hauptmenü und Siberia unverändert. Nicht übernommen: die Logos
FREE RADICAL/EIDOS der Xbox (0x88D8A–0x88E2C, im PC-Code entfernt).
Vermutlich sind alle Teilchen dieses Systems im Port zu klein (ungeprüft).

### Firmenlogos im Startbild

Die Xbox zeichnet im ersten Bildschirm nach dem Schein (PAL 0x88D3B–0x88E2C)
misc/frd und misc/eidos mit Mischart 3, ab Sekunde 1 der Uhr (PC
0x142F12728) mit Farbe 0x320A0A00 | min(Uhr−1, 1)·115, bei (48,370)–(134,420)
und (420,370)–(580,420) im 640×480-Bild, mit noch aktivem Alpha-Test 0x7E.
Der PC-Code hat diese Zeichnungen nicht. ts2.exe hängt sie an den Aufruf
0x1410B58F9 (Mischart 3) an, Lage ×3/×2,25 wie die Abblend-Rechtecke
derselben Funktion (TS2_FIRMENLOGOS=0: aus). Gegen eine xemu-Aufnahme:
Lage auf 1 Pixel gleich, Einblendung ab etwa 1,2 s nach Beginn des Feuers.

### Rechtehinweis vor dem brennenden Logo

Das Titel-Level 0x67 zeichnet in 0x1410D6048 (Xbox PAL 0xDD290) den
Rechtehinweis: igmtex als Verlauf (nur 1/16 der Textur), ts2loading
(0x4F4F4F3F), ts2newlogo, FREE RADICAL und EIDOS, Text 1712 (Stil 0x10)
0x37 Punkte über dem Rand; die Xbox legt den Ladebalken der Ladeanzeige
0xDCE90 darüber (0xDCD90(60,250,392,15) in 512×448 = (75,263)+490×16,
Hintergrund 0x2A18180F, Füllung 0x141E3C7F) und lädt, während das Bild erst
24 % Helligkeit hat. Im PC-Port schrieb igmtex Tiefe und verdeckte alles
außer dem Text; die Logos lagen in 640×480-Punkten, kein Balken, nach
wenigen Bildern weg. ts2.exe zeichnet den Bildschirm nach (Aufruf
0x14114D2C0; Lagen ×3/×2,25, ts2loading unverzerrt 4:3 in der Mitte, Text
in der Höhe wie die gestreckte Xbox-Schrift, 24 % Helligkeit, Balken voll
hell) und hält Level 0x67 im Takt (Aufruf 0x14122C0F6, Zustand 0x142EE3DF0)
mindestens 2 s, bis der Balken voll ist (Enter, Leertaste, Esc, Linksklick,
A/Start überspringen). In Testläufen mit --takte ohne Mindestdauer;
TS2_RECHTE_MS=<ms>, TS2_RECHTEHINWEIS=0 aus.

Umlaute in booklight (`src/host/fontfix.cpp`): book und booklight teilen die
Akzenttabelle 0x14259BC00 (Umlaut = zwei '.', Werte für book). In booklight
(Rechtehinweis, Einsatzbriefing) saßen die Punkte zu klein, zu hoch und
rechts, mit Originalschriften und auf der Xbox ebenso. booklight bekommt eine
eigene Kopie mit Umlaut-Werten 1, −10, 0,45, 1,3; book und impact
unverändert. Proben: TS2_UMLAUTTEST=1, TS2_UMLAUT_LIGHT="x,y,b,h".

## Teilchen und Sprites in Konsolengröße (`src/host/spritefix.cpp`)

Teilchen (0x14112F3C0), Effekt-Objekte (Typen 3, 24, 34, 35 aus
0x1410F5740), muzzle- und glow_64-Sprites rechnen ihre Größe als
Wert · Kamera+0x27C / z in Pixeln einer 480 Zeilen hohen Sicht. Die Xbox
setzt den Faktor auf 60/fov · Sicht+0x20/480 (PAL 0x36DBA, 0x8F6C2), der
PC-Port auf 60/fov · Sicht+0x20/1080 (0x14108363B, 0x1410BC487;
0x1421D3CDC = 1/1080, auch anderswo benutzt): alle diese Sprites waren
2,25-mal zu klein (Werte wie 600/150 für Mündungsfeuer sonst gleich der
Xbox). ts2.exe gibt beiden Schreibern 1/480 und vergrößert die
Obergrenzen (je Teilchen +0x40, 250 in 0x1410F4494) mit Bildhöhe/480;
der frühere Sonderfall in logofix.cpp entfällt (dort nur noch Alpha-Test).
TS2_TEILCHENFIX=0 aus; TS2_SPRITEPROBE=1 zählt die zehn Größenrechnungen.
Gemessen: brennendes Logo unverändert gegenüber dem früheren Sonderfall
(leuchtende Fläche 4,6 %, Xbox 4,1–5,6 %). In Siberia (Tunnel, Schüsse,
Beschuss durch Wachen), Chicago-Start und Glass Smash läuft keine dieser
Rechnungen: Bilder dort unverändert.

Nutzer von Kamera+0x27C: Teilchen (Rauchfahnen der Raketen, Startbild,
Menü), Geschoss-Sprites über die Geschoss-Tabelle 0x142509300 (je 0x40
Byte, +0x20 Fortschreibung, +0x28 Zeichnen; Waffentabelle Modus-Block +4 =
Geschoss + 1): glow_64-Leuchtpunkt von Raketen und Zielsuchraketen
(0x141115044), ElectroTool-Geschoss (0x1411160A2), Geschosse 25/32 und
Explosionen (0x1410F4494, Obergrenze 250), Effekt-Objekte (Pool
*0x142F19500, Listen 0x142F190B0+Typ·8, Typen 3/24/34/35). Gemessen in
Arcade (Mexican Mission, Waffensatz Rockets, `TS2_SPRITEPROBE=1`): Teilchen,
glow_64, 0x1410F4494 und Effekt-Typ 24 laufen; Rauchfahne der eigenen
Rakete bei gleichen Takten mit Fix 1,8- bis 2,5-mal so hoch wie ohne (Puffs
überlappen). xemu (Zustand arcade_rockets, gleiche Menüfolge): dicke
Rauchwolke von 0,14–0,165 Bildhöhe wie mit Fix; ohne Fix nur ein dünner
Strich. Arcade läuft nicht taktgleich zwischen PC und Xbox, deshalb nur
Augenschein. Nicht im Spiel ausgelöst: ElectroTool, Scifi Handgun
(zweiter Modus), Effekt-Typen 3/34/35, 0x1411D81B4.

## Beenden (`src/host/quit.cpp`)

Die Aktion von "Quit" im Hauptmenü (`0x1410C0D80`) setzt nur ein Flag
(`0x142F2C378` = 1). Homefront fragte es über die vtable des
TS2-Laufzeitobjekts ab (+0x38, `0x14122BE88`: "läuft noch" = Flag 0) und
kehrte dann in sein eigenes Menü zurück; ts2.exe fragt es nicht ab, der
Eintrag tat nichts. Der Takt der Hauptmenüseite (`0x1410D134C`) setzt
dasselbe Flag auch bei "Zurück"/"Start" (Tasten 0x120).

ts2.exe lenkt nur die Aktion des Eintrags um (Sprung am Anfang von
`0x1410C0D80`): Sie setzt das Flag wie das Original und schickt dem
Spielfenster `WM_CLOSE`. Danach läuft alles wie beim Schließen des
Fensters (`WM_DESTROY` → `PostQuitMessage` → Ende der Hauptschleife,
Ton aus, Profil per atexit gespeichert). "Zurück"/Esc im Hauptmenü
beendet weiterhin nicht. Eine Sicherheitsabfrage hat TS2 hier nicht.

Geprüft per Skript (`[maus] menue=0`, sonst verschiebt der echte
Mauszeiger die Auswahl): 4× runter, OK bei Takt 580 → `[beenden]`, Ende
nach 581 Takten, Exitcode 0; per `--menuemaus` ebenso; nach Arcade und
Zurück ebenso; Zurück/Start im Hauptmenü beenden nicht.

## Fortschritt (`src/host/profile.cpp`)

TS2 führt den Fortschritt im Profil: 16 Profile je 0x20E4 Byte ab
`*0x143F2F468` (+0 Name, +0x20 Einstellungen, +0xF0 Tasten, +0xD30
Fortschritt), Profil von Spieler i in `0x142F2D6F0[i]`. Das Levelende
(`0x14110B520`) setzt im Fortschritt P (= Profil + 0xD30) bei
P+0xBC8 + Koop·0xC + Schwierigkeit·4 das Bit des Story-Levels (0–9), für
alle Schwierigkeiten bis zur gespielten; Medaillen stehen ab P+0xBD0.
Die Homefront-Fassung legt das Profil bei jedem Start neu an
(`0x14110E7C4`) und speichert nichts; außerdem hat Dambuster fünf
Freischalt-Abfragen durch Flags ersetzt (Story: nur Siberia/Chicago,
Karten: zwei, Figuren und Challenges: alle). ts2.exe setzt die Regeln der
Xbox-Fassung wieder ein (gefunden in der PAL-XBE): Story-Level n auf
Schwierigkeit d, wenn n−1 auf d oder höher geschafft ist (Xbox 0x1AECD0);
Spielarten, Karten und Figuren nach ihren Bedingungen (0x1AED90,
0x1AEEB0/0x1AEDB0, 0x1AEE50); eine Challenge, wenn alle davor im Satz eine
Medaille haben (0x1AEF30). Gesperrtes steht wie auf der Xbox ausgegraut in
den Listen, das Hauptmenü zeigt Arcade und Challenge immer.

Das Profil von Spieler 1 liegt als `profil.bin` neben ts2.exe (0x20E4 Byte,
dasselbe Format wie `TS2Save.bin` von TS2 Redux). Geladen wird es, sobald
TS2 beim Start sein Profil anlegt (den Namen vergibt TS2), gespeichert,
wenn es sich geändert hat: nach jedem Levelende, beim Aufbau des
Hauptmenüs und beim Beenden.

`ts2.ini` [freischalten]: `story`, `arcade`, `challenge`, `figuren`
(Vorgabe 0) schalten wie die Konsolen-Codes sofort alles im Bereich frei
(Schummeln). Test: `TS2_STORY_GESCHAFFT=<index>[:<schwierigkeit>]` trägt
ein Level als geschafft ein. Geprüft: neues Profil → nur Siberia offen;
mit Siberia geschafft → Chicago offen (nur "Leicht"), nach Neustart
weiterhin; Challenge-Sätze schalten der Reihe nach frei.

## Sprache (`src/host/menu.cpp`, `tools/re/xbox_text.py`)

Der PC-Port hat nur die englischen Texte: eine Tabelle von 2770
`char*` ab dem Zeiger `0x1424E2430`. Die Xbox-PAL-Fassung (En, Fr, De, Es,
It) hat in `default.xbe` fünf Tabellen mit je 2770 32-Bit-Zeigern in
derselben Reihenfolge (2386 Einträge wörtlich gleich mit dem PC-Port, der
Rest britische Schreibweise oder Plattformwörter). Gefunden werden sie über
den gemeinsamen Text "Arcade" an Eintrag 422, die Sprache über Eintrag 2142
("Quit", "Beenden", …).

Einmalig aus dem eigenen Image holen (nur lesend): im Launcher unter
Spiel → Sprache das Image wählen und „Texte holen“ (`src/launcher/xboxtext.cpp`,
dort auch die Auswahl der Sprache), oder per Skript (`tools/re/xiso.py`
liest XISO und Redump-Abbilder; beide Wege liefern byteweise dieselben
Dateien):

```bash
python -I tools/re/xbox_text.py "<TimeSplitters 2 (Europe).iso>" game
```

Das schreibt `game/sprache/{de,en,es,fr,it}.bin` (je 2770 nullterminierte
Latin-1-Texte). `ts2.ini` `[sprache] text=de` lässt ts2.exe beim Start den
Tabellenzeiger auf die geladene Tabelle umbiegen; leere Einträge behalten
den PC-Text. Geprüft: Hauptmenü ("Herausforderung", "Optionen",
"Beenden", "Zurück" mit Umlaut) und Optionsmenü auf Deutsch, 2752 von 2770
Texten ersetzt (ab Eintrag 2425 hat die Xbox-Tabelle einen Eintrag mehr; ts2.exe gleicht das aus, sonst stand am Container in Siberia "%d/%d Spieler"). Die Sprachausgabe bleibt englisch.

Die Dateien aus `game/sprache` gehören nie ins Repository.

## Schriften (`src/host/fontfix.cpp`)

Vier Schriften ab `0x14259BF60` (je 0x30 Byte: +0 Texturname, +0x10
Zeichentabelle: je Zeichen ab ' ' u16 x0,y0,x1,y1 in Pixeln der Textur und
s8 y-Versatz, +0x18 Akzenttabelle für Umlaute, +0x20 Zeilenhöhe, +0x24
Leerzeichen, +0x2C Texturnummer): impact, fontcaps (unbenutzt), book und
booklight. 24 Stile ab `0x14259C020` (+0 Schrift, +8/+0xC Maßstab); laufend
`*0x142F999E0` (Schrift) und `*0x142F999E8` (Stil). 0x14121CEC0 setzt die
Schrift-Textur (Nummer `0x1424EDFF0`, Beschreiber `*0x142EE6510`) und ruft je
Zeichen einen Rückruf mit Bildschirm-Rechteck und Texturrechteck in 1/16
Pixel: 0x14121BEFC (Text, auch Umriss-Text 0x14121D740), 0x14121BF68
(Verlauf) und 0x14121C418 (3D-Menüs). Die Rückrufe teilen durch die Größe
des Beschreibers (+0x26/+0x2A), also der geladenen Textur. ts2.exe lenkt die
vier `lea` auf diese Rückrufe (0x14121D574, 0x14121D5FD, 0x14121D6AE,
0x14121DA06) auf Hüllen um: Ist die gesetzte Textur eine Schrift und größer
als ihr Original (Größe aus dem SGI-Kopf der Originaldatei), werden u/v im
Verhältnis vergrößert. Andere Texturen (Tastensymbole über Zeichen 0x0B)
bleiben unberührt; Lage, Breiten und Zeilen ändern sich nicht. Die scharfen
Schriften selbst erzeugt `tools/hd/make_fonts.py` (siehe
docs/hd-texturen.md). Geprüft mit 4fachen Schriften: Hauptmenü,
Pause-3D-Menü, Waffenname, Munition, Untertitel und Umlaute (ö, ü) an
denselben Stellen wie vorher, scharf.
Speicher: Die drei 4fachen Schriften liegen dauerhaft im Spielspeicher
(Arena, 192 MB) und belegen 7 MB mehr als die Originale (7,6 statt 0,5 MB).
Gemessen mit dem Protokolleintrag `[speicher]` (alle 600 Bilder, freier Teil
der Arena `0x142F438E4`): belegt im Menü 36,5 statt 29,5 MB, Siberia 64 statt
57 MB, Chicago 65 statt 58 MB; frei bleiben rund 127 MB. Die D3D-Texturen
werden ersetzt, nicht zusätzlich angelegt (Tabelle mit 4096 Plätzen
unverändert).

## Untertitel (`src/host/subtitles.cpp`)

TS2 untertitelt die Story-Zwischensequenzen selbst: 0x141146C80 zeichnet
die Balken und, wenn 0x14110A380 (liest 0x142F153FC) an meldet, jeden
Untertitel aus der Tabelle der laufenden Sequenz (0x142F3C5F8, Anzahl
0x142F3C678; je Eintrag Beginn s, Dauer s, Farbe grau/rot, Art, Text-Nr.;
13 feste Tabellen, z. B. Siberia 0x142515E50 mit "Nikolai!!!" = Text 2502,
das Spiel-Intro mit 32 Zeilen) mit TS2s Schrift (0x14121D740), mittig an
der Oberkante des unteren Balkens wie auf der Xbox. Die Texte kommen aus
der Texttabelle, folgen also `[sprache] text`. Sprachzeilen im Level
(Big Tony 1150–1163, Durchsagen 1144–1147) sind Skript-Meldungen und hängen
nicht an dieser Einstellung; die Tannoy-Durchsagen in Siberia haben keinen
Text.

Die Einstellung ist "Subtitles" im Optionsmenü Audio/Video (Eintrag 4,
im Profil Einstellungen +0xF0). Ein neues Profil (0x1410BD218) bekommt sie
nur, wenn die Sprache 0x142EE39C0 nicht Englisch ist; der PC-Port kennt nur
Englisch, deshalb fehlten die Untertitel. ts2.exe lenkt den Endsprung
0x14106A159 (Profil → Einstellungen beim Start, 0x1410CC994) um und setzt
danach Profil und 0x142F153FC laut `ts2.ini` `[sprache] untertitel`
(Vorgabe 1). Im Menü umgeschaltet gilt bis zum Beenden.

Geprüft: Siberia-Intro (Skript ohne die OKs bei 1700/1900, Ton an; ohne Ton
steht die Sequenz, ihre Uhr läuft mit der Musik), Takt 2700: "Ohne die
nötige Sicherheitsfreigabe" (text=de) bzw. "We shouldn't be in the research
center" (text=pc), mit untertitel=0 kein Text.

## Fehlerbehebungen nach TS2 Redux (`src/host/fixes.cpp`, `fixes.inc`)

[TS2 Redux](https://github.com/HFTSRedux/TS2Redux) (GPLv3, Fanoto,
DevilDwarf und andere) behebt Fehler des TS2-Ports mit DLLs, die beim Start
Bytes in Homefronts EXE schreiben. Diese Behebungen stehen jetzt als Tabelle
in unserem Code und sind immer an; ts2.exe braucht dafür weder Redux noch
dessen Dateien:

| Gruppe | Inhalt |
|---|---|
| spielfehler | Redux nennt sie Reaper-Spawn- und Regen-Fix; tatsächlich: `0x1411004E4` schaltet die Regentropfen beim Anlegen ab (ts2.exe setzt ihn wieder auf 1, siehe "Bildschirmeffekte"), `0x14122A7AE` ist das `int3` im leeren Stub `0x14122A7AC` (Hitzeflimmern `0x1410EAD20`, Objektzeichnung `0x1411CB5E0`) |
| unsichtbar | Unsichtbarkeitsfehler (`0x1410E4A32`) |
| anzeige | HUD/Text: Schriften, Breiten, Lagen (28 Stellen, ein Code-Stück, 9 Konstanten) |
| menue | Menü-Layout wie auf der Konsole (178 Stellen, vier Code-Stücke, 7 Konstanten) |

- `tools/re/gen_fixes.py` hat die Tabelle einmal erzeugt: es liest die
  Schreibzugriffe statisch aus den Redux-DLLs (capstone, nichts läuft) und
  schreibt je Stelle RVA, neue Bytes und die CRC32 der ursprünglichen Bytes;
  ts2.exe prüft die CRC und lässt eine abweichende Stelle aus.
- Code-Stücke liegen in Füllbereichen von Homefronts EXE (`0x140016EC8`,
  `0x1400258F3` …), die ts2.bin nicht hat; ihre Seiten werden angelegt.
- Redux lenkt Konstanten-Ladungen (`movss xmm, [rip+x]`) auf passende Bytes
  irgendwo in der EXE um (z. B. 2,75 bei `0x140F08693`, außerhalb von
  ts2.bin; ohne sie stürzte die erste Zeitanzeige ab). Die Tabelle enthält
  stattdessen die Werte; ts2.exe legt sie in einer eigenen Seite (RVA
  `0x40000`) ab und richtet die Befehle darauf aus. Auch Werte innerhalb von
  ts2.bin ziehen so um, weil eigene Umlenkungen dort Bytes ändern können.
- Das eigene Hauptmenü liest Lage und Höhe der Seite aus der (geänderten)
  Original-Aufbaufunktion (`0x1410C81EC`/`0x1410C81F6`), so passt es zum
  Konsolen-Layout (ohne Balken, Logo oben).
- Hochaufgelöste Texturen: ts2.exe liest Dateien unter `game/hd/` vor den
  Originalen (`x_fopen`). `tools/re/hd_textures.py` übernimmt die von Redux
  (`ts2_redux.pak`, ein ZIP mit SGI-`.rgb`) ohne Logo, Ladebilder und Banner
  mit Redux-Schriftzug und ohne Xbox-Tastensymbole; eigene Texturen siehe
  `docs/hd-texturen.md`.
- Nicht übernommen: Atemeffekt aus (andere Schreibweise), die eigenen Menüs
  der Redux-DLLs (Speichern/Laden), der NeoTokyo-Patch (Pak-Differenz).

## Ton (`src/host/audio.cpp`, `mixer.cpp`, `vorbis.cpp`)

TS2 mischt selbst. Beim Start meldet ce_sound (`0x14122829C`) seinen Mixer
`0x141227CB4` über `gEnv+0xD0 ->+0x100` an und meldet über das zurückgegebene
Strom-Objekt die Abtastrate (+0x10, 48000). Der Mixer hat die Form
`mix(float* out, frames, rate, channels)`. Er nullt den Block und addiert
80 Stimmen hinein; jede Stimme hat ihre eigene Critical Section, sodass der
Mixer aus einem eigenen Thread laufen darf.

Musik und Sprache laufen über dieselben Stimmen. Die Ogg-Dateien dekodiert
libvorbisfile, das in `ts2.exe` durch stb_vorbis ersetzt ist (ganze Datei
über TS2s Lese-Rückruf in den Speicher, dann dekodieren).

**Stimme** (`0x142FB77E0`, 80 × 0xD8 Byte):

| Offset | Feld |
| --- | --- |
| +0x00 | aktiv |
| +0x04 | Ringpuffer mit 32 Puffernummern (1-basiert) |
| +0x84 | Schleifenanfang |
| +0x88 | aktueller Eintrag |
| +0x8C | Ende |
| +0x90 | Flags; 0x1000 = Musik |
| +0x94 | Schleife |
| +0x98 | positioniert |
| +0x9C | Zustand; 2 = spielt |
| +0xA0 | Spielzeit in Sekunden |
| +0xA4 | Position in Sekunden |
| +0xA8 | Tonhöhe |
| +0xAC / +0xB0 / +0xB4 | Abstand max / min / Abfall |
| +0xB8 / +0xBC | Lautstärke links / rechts |
| +0xC0 | Position relativ zur Kamera: x rechts, y oben, z vorn |
| +0xD0 | `CRITICAL_SECTION*` |

Puffer: `0x142F9B240`, je 0x28 Byte, mit +8 Daten (int16), +0x18 Frames,
+0x1C Rate und +0x20 Kanäle.

**Original-Panning:** nur Stereo, nur waagerecht.
- links = clamp(0,75 − nx), rechts = clamp(0,75 + nx), wobei (nx, nz) die
  waagerechte Richtung ist.
- Abstandsdämpfung: min / (min + Abfall · (d − min)), wobei d auf [min, max]
  begrenzt ist.
- Gemessen: Läuft man vorwärts, zeigt die Bewegung im Ton-Kameraraum auf +z.

**5.1:** Hat das Gerät mindestens sechs Kanäle, mischt `ts2.exe` selbst
(`mixer.cpp`).
- **Stimmen:** dieselben Stimmen mit derselben Fortschreibung von Position,
  Ringpuffer und Schleife.
- **Positionierte Mono-Stimmen:** paarweise mit konstanter Leistung über
  den Azimut auf FC 0°, FR 30°, SR 110°, SL 250° und FL 330° verteilt. Die
  Lautheit ist an Stereo von vorn angeglichen.
- **Übrige Stimmen** (Musik, Menü): wie im Original auf FL/FR.
- **LFE:** bleibt leer.

Auf Stereo-Geräten läuft TS2s eigener Mixer.

**Prüfung** mit der Umgebungsvariable `TS2_TONPRUEFUNG=1`: Etwa jeder 47.
Block wird von beiden Mixern auf demselben Stimmenzustand gerechnet, der
eigene dabei im Stereomodus.
- Ergebnis in Menü, Intro und Siberia mit Schüssen: 158 Vergleiche, davon
  27 mit positionierten Stimmen.
- Größte Abweichung 1,8·10⁻⁷ (Float-Rundung).
- Der Stimmenzustand danach war jedes Mal bytegleich.

**Lautstärken:** Ohne Engine-CVars (`s_MusicVolume`, `s_SFXVolume`,
`0x141226070`) bleiben TS2s Werte bei 1,0. `ts2.exe` setzt sie aus
`ts2.ini` [ton] (`gesamt`, `musik`, `effekte`; `surround=0` erzwingt Stereo).
`ts2.log` protokolliert alle ~5 s den Pegel je Kanal.

### Raum-Hall (`src/host/reverb.cpp`, `[ton] hall`)

**Xbox:** Beim Raumwechsel der Kamera (PAL `0x1C6BA0`, Raum an Kamera +0x294)
setzt sie mit `IDirectSound_SetI3DL2Listener` einen von 90 I3DL2-Sätzen
(`0x3491C8`, Index 9·Gruppe + Effektnummer 0…8). Die Effektnummer je Raum
kommt beim Levelstart (`0x1C71C0`) aus `sound/reverb/levelNN_reverb.s`
("Raumname, Effektnummer") und `bg/levelNN/levelNN.h` (Raumnummern), die
Gruppe aus `0x345108[Index des Levels in 0x2D1390]`. In den PAL-Daten hat
nur Gruppe 1 echte Sätze (2/3 SMALLROOM, 4/5 MEDIUMROOM, 6/7 LARGEROOM,
8 MEDIUMHALL), und nur Level 34 benutzt sie; alle anderen Level, auch
Siberia (Level 35), bleiben auf der Xbox trocken.

**PC-Port:** Kein Code liest die .s-Dateien. Dambuster hat aber
`ts2/sound/reverb/overrides/levelNN.reverb` (26 Level, Zeilen
"Raum : k_rvb_REVERB_PRESET_<Name>") und die Namensliste der 113
EAX-Voreinstellungen (`0x14259CFD0`, ohne Verweis im Code) mitgeliefert.

**ts2.exe:** verfolgt Level (`0x143F27F54`) und Kameraraum
(`*0x143F3A870` +0x2B8) im Ton-Thread, lädt beim Levelwechsel die
Overrides-Tabelle und stellt einen XAudio2-Hall (Submix, I3DL2-Werte der
EFX-Voreinstellungen, `ReverbConvertI3DL2ToNative`, Hallpegel nach I3DL2)
auf die Voreinstellung des Raums. Räume ohne Eintrag bekommen die häufigste
Voreinstellung des Levels (Siberia: STONEROOM, auch für den Starttunnel),
damit Umgebung und Waffen überall hallen. Der eigene Mixer (bei Hall auch in
Stereo) schickt positionierte Stimmen ohne Flag 0x1000 (Musik) und 0x4
(Menügeräusche) zusätzlich in zwei Hall-Kanäle der Quelle, mit dem
Stereo-Panning des Originals und der Wurzel der Abstandsdämpfung.
Unpositionierte Stimmen (Zwischensequenzen, Menü) bleiben trocken.

Proben: `TS2_HALLPROBE=1` (jede neue Stimme mit Flags/Abstand),
`TS2_HALLTEST=<Name>` (eine Voreinstellung in jedem Raum),
`TS2_TONAUFNAHME=<datei.wav>` (Endmischung nach dem Hall als Float-WAV,
relativ zur EXE). `ts2.log` zeigt Raumwechsel ("Hall: Level 35, Raum 5 ->
CITY_STREETS") und im Pegel den Hall-Anteil.

Gemessen (Siberia-Tunnel, Musik 0, Scharfschützengewehr, mit
STONEROOM): Nachklang 0,6–1,0 s nach dem Schuss −23,0 dB statt −27,2 dB
trocken. `TS2_TONPRUEFUNG` mit Hall in Stereo: größte Abweichung 2,4·10⁻⁷.
CPU-Last nicht messbar höher.

## Arcade und Challenge im Spiel

Geprüft per Menümaus-Skript (`--menuemaus`, Koordinaten aus `TS2_MENUSPUR`):

- **Arcade Custom:** Hauptmenü → Arcade → Arcade Custom → Tempo „Normal“,
  dann die Vorgaben mit OK. Das Match auf `l_42_AR` läuft mit Rangliste,
  Bots auf dem Radar und Waffen, ohne Absturz.
- **Challenge:** Hauptmenü → Challenge → „Glass Smash“, erste Challenge
  (`l_35_AR`). Die Spieluhr zeigt nach 35,8 s Spielzeit 00:36,1 bei 60 Hz
  und 00:36,2 bei 120 Hz. Die Spielzeit läuft also auch bei 120 Hz richtig.
- **Ganze Durchgänge per Skript:** Arcade Eigen Deathmatch (3 Punkte,
  2 Min., Bots) bis zur Ergebnisseite (Rang, PUNKTE, BELOHNUNGEN),
  „Hauptmenü“ führt zu den Arcade-Optionen, mit Zurück ins Hauptmenü. Im
  Einzelspieler ist „Wiederholen“ die Überschrift (PC-Layout) und nicht
  wählbar. Challenge „Tod den lebenden Toten“ bis GAME OVER/VERSAGT und
  STATUS (Medaillengrenzen), zurück zur Liste und ins Hauptmenü. Story
  Siberia mit erzwungenem Levelende (Flag 0x8000 in 0x143F2F95C, 0x14110BB60):
  SIEG, `profil.bin` Story 1/1/0, „Neues Feature freigeschaltet“, Chicago
  frei, auch nach Neustart. Kein Absturz.

## Mehrspieler mit geteiltem Bildschirm (`src/host/multiplayer.cpp`)

Arcade mit 2–4 Spielern und Story Co-op zu zweit, wie auf der Xbox: jeder
Spieler mit einem eigenen Pad (XInput). Tastatur und Maus gelten im
Mehrspieler nicht (auch nicht F6, Sprung- und Renntaste in features.cpp).
Mit höchstens einem Pad ändert sich nichts.

**Befund.** Der ganze Mehrspieler steckt noch im Code: Spieleranzahl
`0x143F35188` (Hauptmenü zeigt bei 2 "Story Co-op", sperrt Challenge bei >1
und Story bei >2), ein Profil je Spieler (`0x14106A01C`), eine Kamera je
Spieler (Spieler +8, Ansicht an Kamera +0x238) mit Teilung und HUD je
Spieler. Die Homefront-Fassung hat nur die Seite "nplayers_page"
(Beschreibung `0x1424F2E20`: +0 Takt `0x1410C8FDC`, +8 Zeichnen, +0x20
Aufbau `0x1410C8FD4`) auf "1 Spieler, weiter ins Hauptmenü" verkürzt (`mov
ecx,1` bei `0x1410C8FF9`, `mov [0x143F35188],1` bei `0x1410C8FFE`). Pads:
8 Pad-Daten je 0x60 Byte ab `0x142515350` (+0x14 0x63 verbunden, +0x20/+0x24
Knöpfe vorher/jetzt, +0x28/+0x2C gedrückt/losgelassen, +0x30/+0x38 linker
Stick x/−y, +0x34/+0x3C rechter Stick x/−y, +0x44/+0x45 Motoren), Spieler i
liest Port `0x142515C50[i]` (7 = keins). Diese Zuordnung kopiert
`0x1411458F4` je Takt aus der Port-Suche `0x141225878`, die im PC-Port nur
Eintrag 0 setzt; `scePadRead` (`0x141225898`) füllt nur diesen einen Port.

**Spieleranzahl.** Stecken mindestens zwei Pads, zeigt die Seite "1 Player"
bis "4 Players" (Texte 454–457, Titel 2116; Einträge über der Zahl der Pads
ausgegraut), Kopf und Maße wie "Arcade Type", Zeichnen wie das Hauptmenü
(`0x1410C1E9C`). Die Wahl führt den Original-Takt mit N statt 1 aus (beide
Immediates werden für den Aufruf umgeschrieben). Kreis/B im Hauptmenü führt
zurück zur Auswahl, wenn mindestens zwei Pads stecken oder schon mehrere
spielen. `ts2.ini` `[mehrspieler] an=0` schaltet die Auswahl ab.

**Pads.** Spieler 1 behält sein Pad (input.cpp, Pad-Modus Port 0), Spieler
2–4 bekommen bei der Wahl die weiteren angeschlossenen Pads der Reihe nach;
die Zuordnung bleibt bis zur nächsten Wahl. Hüllen um Port-Suche und
`scePadRead` setzen Port i für Spieler i+1 und füllen die Ports 1–3 mit
derselben Übersetzung wie `[pad]` (eigener Zustand je Pad: Akkorde,
Zielen umschalten). Rütteln je Pad aus +0x44/+0x45, Springen und Rennen
auch für Spieler 2–4. Profile: Spieler 1 bekommt profil.bin und seinen
Windows-Namen, die übrigen "Player 2" … (nicht gespeichert).

**Teilansicht löschen.** Das Löschen `0x14122A4FC(x0, y0, x1, y1, rgba)`
löschte im PC-Port das ganze Ziel (ClearRenderTargetView) statt des
Rechtecks; die zweite Ansicht löschte so die erste. Spielen mehrere, löscht
ts2.exe nur das Rechteck (`ClearView`, D3D 11.1), die Tiefe ganz.

**2D in Teilansichten.** TS2 zeichnet HUD, Texte, Menübögen und
Bildschirmeffekte in Bildschirmpixeln (Konsole: vortransformierte Ecken, nur
von der Schere begrenzt). Der PC-Port rechnet sie im Zeichenzustand
`0x141229434` (spec-Bit 0x20) mit einer festen Matrix für 1920×1080 in den
Clip-Raum um und zeichnet sie im gesetzten Bildausschnitt (`0x14122AA04`,
einziger Aufruf `0x141088C88`). In Teilansichten wurde 2D dadurch in den
Ausschnitt gestaucht (HUD halb so hoch, Nachbild der Sichtart als
Geisterbild) und an dessen Rändern abgeschnitten. Mit mehreren Spielern
merkt sich ts2.exe den Ausschnitt des Spiels und setzt vor jeder Zeichnung
(Aufrufe von `0x141229434` bei `0x141229953`/`0x141229EB4`) für 2D einen
Ausschnitt über das ganze Ziel, für 3D wieder den des Spiels. (Eine frühere
Abbildung der 2D-Matrix auf den Ausschnitt griff nur beim Neubau der Matrix;
die Matrix wird nur bei neuer Zeichenart gebaut, `0x142FE3828`.)
`TS2_MP2D_ALT=1` zeigt 2D wie im PC-Port.

**Figurenwahl mit mehreren Spielern.** TS2 Redux (fixes.inc, Gruppe
„menue“) legt die Figurenwahl für einen Spieler wie auf der Konsole aus.
Mit mehreren Spielern zerfiel sie damit: Der Aufbau `0x1410D216C` rechnet
die Höhe eines Spielerfelds als (1 bzw. 0,7) · 70 · 3,0 (Faktor bei
`0x1410D21E8`) · Zeilen, Zeilen = 4 allein bzw. 3 mit mehreren; Redux setzt
3/2 (`0x1410D21F2`), die Felder waren zu niedrig (Name, Figur abgeschnitten).
Die Porträts (`0x1410BC8D4`, Redux-Stellen `0x1410BC9C8`/`9CB`/`9D4`/`9DD`)
blieben leere Vierecke. fixes.cpp merkt sich die Originalbytes jeder Stelle
(`fixes_original`); bei mehreren Spielern gelten an diesen Stellen die
Originalbytes, und der Faktor sinkt auf 2,75 (2 Spieler) bzw. 2,5 (3–4,
eigene Wahl), damit beide Feldreihen über die Fußzeile der Redux-Seite
(Höhe 992, `0x1410D22A6`, bleibt) passen. Mit einem Spieler wieder Redux.
Geprüft: 2/3/4 Spieler vollständig, Match startet; Einzelspieler
unverändert. Offen: bei 3/4 Spielern liegen die Wertezeilen eng; die
Ergebnisliste nach dem Match ist ungeprüft.

**Pausenmenü in Teilansichten.** Der Menücode ist derselbe wie auf der
Xbox (Update `0x1411B8FC0` = Xbox `0xD9380`, Seite `0x1411B33D8`/`0x1411B36DC`
= `0xD3A70`/`0xD3CC0`, Bögen `0x14113C478` = `0xC4F80`). Mit mehreren
Spielern rückt `0x14122AF60` die Menüansicht an den Bildschirmrändern ein
(Fernsehrand), `0x14122AD80` liefert Randabstände ±48, `0x14122AE48` die
Ansichtsmitte ±24, die Seitenstriche liegen bei oben + 28 bzw. 38. Das sind
Xbox-Pixel (640×480), im PC-Port unverändert: Die Überschrift lag am oberen
Rand und wurde abgeschnitten, die Striche über der Überschrift. Mit mehreren
Spielern rechnet ts2.exe sie ·3 waagrecht und ·2,25 senkrecht: Einrückung
114/85 (Hülle um die Aufrufe `0x1411B93CD`, `0x1411B1741`), beide
Funktionen ersetzt, Striche über die Operanden bei
`0x1411ACC1D`/`0x1411ACC27` (63,0 und 85,0). Ring und Sicheln sind am PC
1,3-mal so hoch wie auf der Xbox (Mehrspieler-Höhe 220, Redux 255, imm bei
`0x14113C53B`); allein staucht die 2D-Abbildung in die Menüansicht (80 % der
Bildhöhe) sie passend, im Mehrspieler lädt der Befehl 180. Dieselbe
Stauchung ist der Grund für die Textkorrektur in menu.cpp
(`v' = 0,0142 + 1,2462·v`): Texte sind 2D, Balken 3D; im Mehrspieler
entfällt sie. `TS2_PAUSE_ALT=1` zeigt das Pausenmenü wie vorher. Geprüft
(Arcade 2 Spieler Pause P1/P2, 3 Spieler P1/P3, 4 Spieler P4, Story Co-op
P1/P2): wie Xbox; Einzelspieler (Arcade, Siberia; Pause und Spiel)
pixelgleich. Porträtleiste eines bestätigten Spielers: auf der Xbox
ebenfalls leer. Rang-Blase: Größenverhältnisse wie Xbox (RANK 0,57 der
Blasenbreite, Xbox 0,58); das englische „1st=“ ist nur breiter als das
deutsche „6.“.

**Ergebnisseite „GAME OVER“ im Mehrspieler.** Seite `0x14257C6E8` (Zeichnen
`0x1411B59C4`, Aufbau `0x1411B60A0`; Xbox `0xD6170`/`0xD6880`). Xbox: je
Ansicht Überschrift „GAME OVER“ (Balken v 0,52..0,4), darunter der Rang in
einem Textfenster, beim ersten Menüspieler die Einträge (Balken
−0,02..−0,14, −0,17..−0,29). Der PC-Port hat den Aufbau verändert:
Überschriftbalken aus einer Tabelle (`0x14257BFD4`/`0x14257C084`:
0,25..0,13), Eintragsbalken 0..0, und beim ersten Menüspieler überschreibt
der erste Eintrag („Rematch“) Text und Aktion der Überschrift. Das
Rangfenster (Seite +0x2C: y0 = Seite y + 47, Höhe 75) rechnet in
Xbox-Pixeln ohne den halben oberen Randabstand der Xbox, und TS2 Redux
(„anzeige“, `0x1411B5AAF`, `0x1411B5AE7`, `0x1411B5B77`) passt es für einen
Spieler an; im Mehrspieler lag der Rang über bzw. unter der Ansicht. Mit
mehreren Spielern gelten an den drei Redux-Stellen die Originalbytes; nach
dem Aufbau stellt ts2.exe Überschrift und Einträge wie auf der Xbox her
(Balkenobjekte Menü +0x3D0/+0xDE0/+0x17F0, Textlagen neu über
`0x1410DEEBC`), und das Rangfenster (vor `0x1412230DC`, Aufruf
`0x1411B5B05`) liegt bei Seite y + (47 + oben·0,5)·2,25 mit Höhe 75·2,25.
Geprüft mit 2 und 3 Spielern (Arcade): wie Xbox, die Schrift ist größer
(wie allein). Einzelspieler unverändert (Redux-Werte, Seite unverändert).

Soll (Xbox-PAL in xemu, 2 Spieler Story Co-op, eigene Aufnahme): Das Menü des
pausierenden Spielers füllt dessen ganze Ansicht ohne Randbalken; Überschrift
vollständig sichtbar (normiert y ≈ 0,82), Einträge auf ihren Balken, Bögen
über die ganze Breite und innerhalb der Höhe (flacher als allein); Schrift in
normaler Pixelgröße, nicht gestaucht. Der andere Spieler sieht „Spiel
unterbrochen / Warte auf andere Spieler“ mittig. Aufnahme: xemu mit Port 1 =
Tastatur, Port 2/3 = echte Pads (SDL-GUID, eigene Konfiguration), Eingaben
für Spieler 2 über den gdbstub von QEMU (Haltepunkt am Pad-Lesen, PAL 0x1BA550
und 0x1BA568, Pad-Daten 0x2CDB48 + 0x84·Port, Knopfbits für eine Lesung
gesetzt); die Pads müssen dabei eingeschaltet sein.

**Xbox-Soll Arcade** (eigene Aufnahmen, 2 und 3 Spieler, Deathmatch
Mexiko). 2 Spieler oben/unten, 3–4 in Vierteln (PC 0x1411D4734 wie Xbox
0x1409F0). HUD-Elemente (Rang, Radar, Munition) in jeder Ansicht in
derselben Pixelgröße wie allein, in festen Abständen von den Ecken der
Ansicht; Pausenmenü füllt die Ansicht des pausierenden Spielers;
Figurenwahl mit schmaleren Feldern (2: übereinander, 3–4: 2×2), Porträts
quadratisch. Nach genauerer Messung bildet die 2D-Hülle TS2s
Bildschirmpixel richtig ab; die Abweichungen (Radar klein bzw. gestaucht,
Rang-Abzeichen gestaucht, Porträts verzerrt, Pausenmenü zu groß) entstehen
in den Größenrechnungen der Elemente (Ansicht +0x28/+0x2C, Merker
+0x30/+0x34; Radar 0x141138BC4, HUD 0x141140A00, Redux-Gruppe „anzeige“).

**HUD und Figurenwahl mit mehreren Spielern.** Radar (0x141137244):
Halbmesser 35 allein (0x1411376B6), 30 mit mehreren Spielern (0x1411376AB,
Ansicht +0x30), Breitenfaktor 3,75 (0x141137710); TS2 Redux setzt für
allein 64 und 1,0, der Mehrspieler-Zweig blieb bei 30 (winziger Kreis). Mit
mehreren Spielern lädt dieser Zweig denselben Wert wie allein: rundes Radar
in gleicher Pixelgröße wie auf der Xbox. Rang und Munition stimmten schon
(feste Abstände von der Ansichtsecke; die Xbox rückt sie weiter ein, Rand
für Fernseher). Figurenwahl-Porträts (0x1410BC8D4): halbe Breite = halbe
Höhe · 0,95 · 1920/512 (0x1410BC98D, 0x1410BC995), ein 3,56-fach breites
Viereck, von der Zelle abgeschnitten; mit mehreren Spielern laden beide
Faktoren 1,0 (quadratisch wie Xbox). Alles nur bei der Wahl von 2–4
Spielern, mit einem Spieler wieder Original/Redux. RANK: Das „=“ ist TS2s
Gleichstand-Anzeige; auf der Xbox trat im aufgenommenen Match kein
Gleichstand mit „=“ auf.

**Testen ohne Pads:** `TS2_MPSKRIPT="spieler:takt:knoepfe,lx,ly,rx,ry,lt,rt[,ts2bits];…"`
(Spieler 2–4, sonst wie `TS2_PADSKRIPT`, das Spieler 1 übernimmt),
`TS2_MP=<n>` (Anzahl ohne Menü), `TS2_MPPROBE=1` (Knöpfe, Motoren, Lage).
Geprüft: Arcade Custom Deathmatch mit 2, 3 und 4 Spielern (oben/unten bzw.
Viertel, HUD je Spieler an Ort und Stelle; Gehen, Rennen ×1,6, Drehen,
Sprung, Feuer, Rütteln), Story Co-op Siberia (Spieler 2 läuft aus dem
Tunnel, kein Geisterbild), Rückweg zur Auswahl. Einzelspieler (Siberia mit
Tastatur und nur mit Pad): pixelgleich zum Stand davor.

**Offen:** mit echten Pads prüfen; Auflösungen ungleich 1920×1080;
Pads müssen vor der Wahl stecken.

## Bildanpassung (`src/host/postfx.cpp`)

`[grafik] gamma`, `kontrast`, `helligkeit` und `saettigung` passen das Bild
als letzten Schritt vor der Ausgabe an.

- **Alle Werte neutral (Vorgabe):** Der Schritt entfällt, TS2 zeichnet wie
  bisher direkt in den Bildpuffer.
- **Sonst:** TS2 zeichnet in eine eigene Textur gleicher Größe, die auch als
  Quelle seiner Bildschirmkopien dient. Ein Vollbild-Dreieck schreibt sie
  angepasst in den Bildpuffer. Alle Zustände, die dieser Schritt anfasst,
  werden danach wiederhergestellt.
- **Geprüft** (Siberia, 60 Hz): Der Schritt mit praktisch neutralen Werten
  ergibt ein pixelgleiches Bild (größte Abweichung 0). Kontrast 1,2 /
  Gamma 1,1 erhöht die Helligkeitsstreuung von 29 auf 35.
- **HDR** (`[grafik] hdr=1`, Vorgabe): Läuft der Monitor des Fensters im
  HDR-Modus (Windows "HDR verwenden", `IDXGIOutput6::GetDesc1` meldet
  G2084/P2020), ist der Bildpuffer scRGB (`R16G16B16A16_FLOAT`,
  `DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709`). TS2 zeichnet weiter in eine
  UNORM-Textur (seine Kopien brauchen dieses Format); der Schritt
  linearisiert (sRGB-Kurve) und legt Weiß auf `[grafik] hdr_weiss` Nits
  (Vorgabe 200). Vorher bildete Windows (bzw. RTX HDR) das SDR-Bild selbst ab,
  und im HDR-Modus wirkte Siberia überstrahlt. Ohne HDR-Modus bleibt alles
  SDR wie bisher. Bildschirmfotos (`--bild`) kommen aus TS2s Textur, also
  ohne diesen Schritt. Geprüft: läuft auf einem HDR-Monitor ohne
  D3D-Meldungen; die Helligkeit am Gerät ist noch von Hand zu prüfen.

## Supersampling (`src/host/supersample.cpp`, `[grafik] supersampling`)

`[grafik] supersampling=k` (Vorgabe 2; 1 = aus, 1.5, 2, 3; `TS2_SUPERSAMPLING=k`
hat Vorrang; Launcher: Grafik → Supersampling). TS2 und alle Korrekturen
rechnen weiter in der Render-Auflösung (1920×1080: 2D-Matrix, Ansichten,
Schere, Löschen, Bildkopien, Proben); gezeichnet wird k-fach so groß, die
Ausgabe gefiltert auf Fenster bzw. Monitor (Flächenfilter; k=2 auf einem
4K-Monitor 1:1, auf 1080 Zeilen Mittel aus 2×2; vergrößern Catmull-Rom).

- k-fach angelegt: die Szene (postfx.cpp), TS2s Tiefenpuffer (0x14122B78C)
  und seine beiden Bildkopien (0x14122B458 → 0x14122B334, Tabelle
  0x142FE59A0), erkannt in CreateTexture2D an Größe, Bindung und Aufrufer
  (TS2-Code). Geladene Texturen (0x14122B438, mit Mipmap-Erzeugung) nicht.
- Kontext-Hülle: Ansichten und Scheren werden beim Setzen auf ein k-faches
  Ziel umgerechnet, beim Lesen bekommt TS2 seine Werte zurück (0x14122A1D0
  liest sie am Bildende, 0x14122B524 setzt sie wieder). Kopien zwischen
  k-fachen Texturen (0x14122B384) mit k-fachem Rechteck; Kopien aus dem Bild
  in kleine Texturen (Bildschirme mit Text 0x14122C5D0, Scanner) gefiltert
  verkleinert. Verdeckungsabfragen (0x14123157C) melden Pixel / k².
  Linien (Scanner, Handkarte) zeichnet ein Geometrie-Shader k Pixel breit,
  sonst würden sie dünn und blass. Mehrspieler: ClearView der Teilansichten
  rechnet das Rechteck um.
- Der Bildpuffer hat mit k>1 die Fenstergröße und folgt ihr (ResizeBuffers,
  Alt+Enter). k=1: alles wie vorher (Bildpuffer in Render-Auflösung).
- Bildschirmfotos (--bild, --serie) bleiben 1920×1080 (verkleinert);
  TS2_BILD_VOLL=1 speichert die k-fache Szene, TS2_AUSGABEBILD=1 (SDR) den
  ausgegebenen Bildpuffer. TS2_SSPROBE=1 zählt Zeichnungen je Grundform.
- Geprüft: k=1 pixelgleich zu vorher (Startbild, Menü, Siberia bis 4600,
  Pausenmenü). k=2/3: Menü, Startbild, Siberia (Zaun, Scanner), Chicago,
  NeoTokyo (Regen auf der Kamera), Wild West (Mündungslicht), Sichtart 7,
  Zielfernrohr, Mehrspieler 2 Spieler (Teilansichten, HUD, Pause): gleiche
  Bilder mit glatteren Kanten, keine D3D-Meldungen. Vollbild 4K mit k=2:
  Ausgabe gleich der Szene (1:1); Alt+Enter 4× hin und zurück.
  GPU-Zeit je Bild (Siberia draußen, RTX): k=1 0,24 ms, k=2 0,51 ms,
  k=3 0,86 ms; Bildtakt in allen Fällen 60,0 Bilder/s.
- Kleiner Unterschied: Kanten mit Alpha-Test werden geglättet (brennendes
  Logo: leuchtende Fläche 3,1 % → 2,8 %). Offen: HDR-Helligkeit am Gerät,
  Fenster ziehen von Hand, Verdeckungsabfragen an einem Lichthof gegenprüfen.

## Schatten (`src/host/shadowfix.cpp`, Kontext-Hülle `ctxproxy.h`)

Kisten warfen im PC-Port schwarze Flecken (Xbox: schwache, weiche
Abdunklung). Befund mit `TS2_DRAWDUMP=<takt>` (protokolliert jede Zeichnung
mit gewöhnlicher Alpha-Mischung, schreibt ihre Texturen roh heraus;
`TS2_DRAWSKIP=<textur>`, `TS2_DRAWSKIPN=<nummern>`, `TS2_DRAWSHADER=<textur>`
mit Pixel-Shader, Eingabe-Layout und Ecken):

- Schattenflecken sind Vierecke knapp über dem Boden mit einer schwarzen
  Textur, deren Alpha die Form trägt: 16×16-Klecks (1847, Alpha bis 0,62),
  Kistenumriss 1760, Fleck unter Fässern 1875, Tisch und Stuhl (32×32) usw.
  Gezeichnet mit SRC_ALPHA/INV_SRC_ALPHA; Deckkraft = Textur-Alpha ×
  Eckenalpha. Die Texturen sind gleich der Xbox (docs/alpha-texturen.md).
- Das Eckenalpha stimmt nicht: Mit Licht (spec 0x8284, Bit 0x200) zählt
  TS2s Vertex-Shader je Lampe 1 dazu (Umgebungs-Alpha 0), also ≈ 3: alles
  über einem Drittel Textur-Alpha wird voll schwarz (harte schwarze Flächen
  unter Tischen, Stühlen, Kisten in Chicago). Mit Eckenfarbe (spec 0x8016)
  liest das Layout an 40 Unsinn (oberstes Byte eines Floats, 0,26–0,75).
  Der Klecks in Siberia wird mit beiden gezeichnet.
- Abhilfe: Zeichnungen mit schwarzer Alpha-Textur (bis 256×256) und spec
  mit Bit 0x3 oder 0x200, ohne Levelgeometrie (0x20000000) und 2D (0x20),
  bekommen einen eigenen Pixel-Shader: Textur-Alpha × `[grafik] schatten`/0,62
  (Vorgabe 0,62 = Textur-Alpha unverändert, abgeglichen mit Bildern der
  Xbox-Fassung in xemu; 0 = aus). spec liest ts2.exe aus 0x142FE3828.
- Geprüft: Chicago Bar (Tisch, Stühle) und Lager (Kiste) vorher/nachher bei
  gleicher Kameralage; Siberia-Tunnel wie vorher; Zielfernrohr,
  Einschusslöcher, HUD unverändert.
- Die D3D11-Laufzeit schreibt die Funktionstabelle ihres Kontexts selbst um;
  umgelenkte Kontext-Methoden hielten nicht (auch der frühere Befund "TS2
  löscht nie" war deshalb falsch: es löscht auf 0,0,0,0). Deshalb bekommt TS2
  eine Hülle um den Kontext (`ctxproxy.h`, Weiterleitungen aus d3d11.h
  erzeugt mit `tools/gen_ctxproxy.py`).

## Transparenz (`src/host/alphafix.cpp`)

Die Laserschranken der Zellen in Siberia (Raum 74, unteres Geschoss)
erschienen als schwarze Rechtecke mit dünnen roten Linien, im Licht als
rote Flächen. Befund:

- Die Schranke ist `ob/dam/damlaserdoor.xbr`, ein Viereck mit Textur 2452
  (roter Strahl auf Schwarz, Eckenfarbe 0x7F00007F), gezeichnet mit
  SRC_ALPHA/INV_SRC_ALPHA.
- Auf der Xbox (`2452.xbt` im Pak, DXT3) trägt der Alphakanal Strahl und
  Schein, sonst ist Alpha 0. In den PC-Daten (`textures/2452.rgb`) ist
  Alpha überall 255. Das Schwarz deckt so den Raum ab; mit Licht wird die
  Fläche rot.
- Ein Vergleich aller Paks findet mehr solcher Texturen: deckende `.rgb`,
  deren `.xbt` ein Alpha hat, das der Helligkeit folgt. Das sind Leuchten,
  Mündungsfeuer und Funken (z. B. 1442, 1465, 1487, 2543, 3037).
- Abhilfe (`[grafik] transparenz=1`, Vorgabe): Bei Zeichnungen mit
  SRC_ALPHA/INV_SRC_ALPHA prüft ts2.exe die Textur einmal am Inhalt. In
  Frage kommen Texturen von 4×4 bis 64×64, deren Alpha überall 255 ist, mit
  schwarzen Ecken, mindestens einem Viertel schwarzer Fläche und hellen
  Stellen. Solche Texturen bekommen für die Zeichnung eine Kopie, deren
  Alpha der größte Farbkanal ist. Für 2452 ist das genau das Xbox-Alpha. TS2s
  Shader (Licht, Eckenfarbe, Alphatest) bleibt unverändert.
- Ganz schwarze Texturen (`misc/black1x1`, Abblenden) und größere Texturen
  (Figuren, Schilder) bleiben, wie sie sind.
- Geprüft: Laserzellen in Siberia, vorher und nachher, bei gleichem Takt
  (freie Kamera). Ohne Änderung: Siberia-Tunnel mit Schüssen, Glass Smash,
  Hauptmenü, Levelwahl. Dort hat keine weitere Textur die Bedingung
  erfüllt.
- Proben dazu: `TS2_DRAWARTEN=5*6,5*2,aus|alle` wählt die Mischarten für
  `TS2_DRAWDUMP`. Das Protokoll nennt je Zeichnung Mischart, Größe und
  Inhalts-Hash der Textur (FNV-1a der obersten Stufe, Zeilen wie in der
  `.rgb` von unten nach oben). `TS2_TEXWATCH=<hash>,...` meldet in jedem
  Takt Kameralage und Mischung, sobald eine solche Textur gezeichnet wird;
  beim ersten Mal kommen Shader und Ecken dazu.

- Vollständiger Vergleich mit dem Xbox-Image: docs/alpha-texturen.md. Alle
  betroffenen Texturen (25 deckende, 2 fast deckende) sind Leuchttexturen,
  deren Farbe schon mit dem Xbox-Alpha multipliziert ist; kein
  Schattenproblem geht auf fehlendes Alpha zurück.
- Genau statt geschätzt: `python -I tools/re/xbox_alpha.py <xbox.iso> game
  --bin game/xbox_alpha.bin` schreibt aus dem eigenen Image je Inhalt
  (Hash der geladenen Datei) den Xbox-Inhalt (Alpha, Farbe der Xbox); ts2.exe
  lädt die Datei beim Start und bindet für solche Texturen (bis 256×256) eine
  Kopie. Fehlt die Datei, bleibt die Schätzung aus der Helligkeit.

## Bildschirmeffekte (`src/host/effectfix.cpp`)

**Regen auf der Kamera.** Die Tropfen (0x141101158, `misc/camsplash`) zeichnet
TS2 zweimal: mit Mischart 8 (DEST_COLOR·Quelle + Ziel, aufhellen), dann versetzt
mit Mischart 9. Der PC-Port legt Art 9 als DEST_COLOR/ONE mit SUBTRACT an:
Quelle·Ziel − Ziel ist überall negativ, wo die Textur nicht weiß ist – die
schwarzen Quadrate (TS2-Redux-Issue #3). ts2.exe ersetzt die 16 Zustände von
Art 9 in TS2s Tabelle (0x14301ECE0) durch REV_SUBTRACT (Ziel·(1 − Quelle),
abdunkeln; nur der Regen benutzt Art 9) und schaltet die Tropfen wieder ein
(0x1411004E4 = 1, Redux setzt 0; `TS2_REGENAUS=1` lässt das). Geprüft:
NeoTokyo-Start, Tropfen statt Quadrate.

**Licht von Mündungsfeuer und Feuer** (Redux-Issue #4). TS2 hat ein
Shader-Paar (ts.vp/ts.pp). Der Pixel-Shader (Levelgeometrie, spec
0x20000000) rechnet die Lichter gegen SV_POSITION (Bildschirmpixel) und
dämpft mit lightpos.w/lightcol.w, die TS2 als 0/1 übergibt: ein Schuss
erhellte Wände im ganzen Bild. Die Lichter liegen dort im Modellraum (Level =
Welt; gemessen: Mündungsfeuer 1,6 vor der Kamera, falloff 0/4/0,25). Der
Vertex-Shader (Figuren, spec 0x200) zieht die projizierte statt der
Sichtraum-Lage ab. ts2.exe setzt für diese Zeichnungen ein nachgebautes Paar
mit richtigen Lagen und der Dämpfung aus `falloffs` (min, max, 1/(max−min),
wie TS2s Vertex-Shader) ein; spec liest es aus 0x142FE3828. Ohne Licht
bildgleich. `TS2_LICHTFIX=0|1|2` (aus/Level/Figuren), `TS2_EFFEKTFIX=0` alles
aus. Geprüft: Wild West mit Schüssen.

**Rote Figuren in Chicago (Raumhelligkeit).** Figuren und die eigene Waffe
nehmen als Umgebungslicht (cb0 ambient) die Farbe ihres Raums aus der Tabelle
0x143F33CC0 (float[3] je Raumnummer, gelesen von 0x1411CA054; < 0: grau 0,3).
Die Xbox kopiert dafür beim Levelstart `bg/ambient/level<n>_ambient.raw`
hinein. Der PC-Code (0x1410F6B74, Aufruf 0x1410F7A96 in 0x1410F78A4) liest
stattdessen die Textfassung `bg/level<n>/level<n>.h` (Raumname → Nummer) und
`level<n>_ambient.s` in zwei feste, aufeinanderfolgende Puffer zu 0x2800 Byte
(0x142F197B0, 0x142F1BFB0). Die Chicago-.h hat 10 469 Byte: Der Puffer endet
in `ROOMNUM_ak8 15` ohne Nullbyte, die Suche läuft in die .s weiter, und Raum
15 (Ziegelraum am Lager) bekommt die rotbraune Farbe von ak8 (0,231/0,088/0,082
statt olivgrau 0,2375/0,2321/0,1222); die Räume dahinter fehlen (grau).
Ebenso betroffen: Level 41 (Robot Factory, 46 Räume). ts2.exe lenkt den
Aufruf um und legt nach der Textfassung die .raw darüber (wie die Xbox);
`TS2_RAUMLICHT=0` schaltet das ab. Proben: `TS2_LICHTPROBE=1`,
`TS2_SPRUNG=x,y,z[,raum]`, `TS2_RAUMSUCHE=<raum>`. Geprüft: Chicago Raum 15
(Waffe vorher rot, jetzt olivgrau wie xemu); Wild West: Mündungsfeuer weiter
nur örtlich.

**Fotokamera (Redux-Issue #21).** Der Sucher der Digitalkamera ist Sichtart
11 (0x14113A6BC, Xbox PAL 0xC3840), nicht 7. Wie Sichtart 7 (0x141139844)
zeichnet er den Rand erst in RGB, dann mit Maske 0xFFFFFF in den Alphakanal
und mischt zuletzt die Kopie des vorigen Bilds (0x14122B74C(3)) mit
DEST_ALPHA über die Ansicht. Innen muss das Ziel-Alpha 0 sein; im PC-Port
steht dort 1 (TS2 löscht den Bildpuffer nicht je Bild, die Szene schreibt nur
RGB). Dadurch übernahm der Sucher innen das vorige Bild: eingefroren,
verschmiert beim Zoom, nach dem Blitz dauerhaft weiß. Für Sichtart 7 setzt
der PC-Port selbst ein Rechteck mit Farbe 0 und Art 0 (0x141139B64), bei 11
fehlt es. ts2.exe setzt es ein (effectfix.cpp, Haken auf 0x14113A977,
Rechteck 0x14108A334 über das Innere, Mitte ±0,26·Breite).
`TS2_KAMERAFIX=0` schaltet das ab. Geprüft (Kamera per Testhilfe gegeben,
Gegenstand 35): NeoTokyo und Siberia, Sucher live, Blitz, danach live. xemu
nicht verglichen. Proben: `TS2_KOPIEPROBE=1`, TS2_DRAWDUMP zeigt Mischart und
Lichter. Nebenbefund: 0x14122AA90 (Scissor) begrenzt fest auf 1920×1080.

## HUD, Bildschirme und Figurenwahl (`src/host/hudfix.cpp`)

### Figurenwahl: Absturz nach vielen Figuren (Redux-Issue #5)

**Befund.** Wer in Arcade → Eigen → Figurenwahl lange blättert, bringt
ts2.exe nach rund 90 Figurwechseln zum Absturz in `d3d11.dll`
(`UpdateSubresource` aus 0x14122A21F, Ziel z. B. 0x1580000040E). Ursache
ist die Texturverwaltung des Ports (dx11.cpp):

- TS2 führt Texturen über Beschreiber (Bank `*0x14259D5E0`, je Level 1024
  zu 0x38 Byte; +0x18 Flags, +0x1C Texturnummer, +0x34 D3D-Nummer). Ein
  entladenes Modell gibt seine Beschreiber frei (0x14107387C → 0x14122C918
  setzt Flag 0x800000), und sie werden wiederverwendet, sobald die Bank leer
  ist (0x14122C33C).
- Der Port legt je Beschreiber eine D3D-Textur an (0x141229FD0, Tabelle
  0x142FE59A0 mit 4096 Plätzen, Zähler 0x143020A80 wächst nur bis zum
  Levelwechsel) und hängt die Nummer an eine Liste 0x142FFDB60 (1024
  Plätze, Zähler 0x143020A90, von niemandem gelesen). Die alte D3D-Textur
  eines wiederverwendeten Beschreibers gibt er nie frei.
- Jede neue Figur lädt 8–10 Texturen. Nach rund 90 Figuren läuft die Liste
  über; dahinter liegen Zeichenzustände und die Upload-Warteschlange
  (0x142FFEB90), deren erstes Ziel überschrieben wird. Mehr Spielspeicher
  hilft nicht. Die Redux-Umgehung (nop bei 0x1410D068B) unterdrückt nur das
  Laden der Modelle.

**Abhilfe.** ts2.exe ersetzt 0x14122C33C (gleiche Logik) und gibt beim
Wiederverwenden eines Beschreibers dessen alte D3D-Textur frei (nicht, wenn
sie ein Render-Ziel ist oder noch in der Upload-Warteschlange steht). Das
Anlegen einer D3D-Textur (Aufrufe 0x14122B35A, 0x14122B44E) nimmt zuerst
einen so frei gewordenen Platz. Die Liste 0x14122B41C bekommt eine Grenze.

**Geprüft.** Skript Arcade → Eigen → Figurenwahl, alle 12 Takte "runter":
vorher Absturz nach 45 s (Takt ~2750, 93 Modelle), jetzt 20 100 Takte
(~1500 Figurwechsel, 10 361 D3D-Texturen wiederverwendet) ohne Absturz,
D3D-Zähler bleibt bei 1026. Nach 3800 Wechseln zeigen die Modelle richtige
Texturen, das Match danach (Mexiko) startet. Probe: `TS2_HUDPROBE=1`
(Zähler je Figur, bei Abstürzen Modul und Abwicklung bis in den TS2-Code).

### Gegnerbalken der Endgegner (Redux-Issue #20)

**Befund.** 0x141140590 zeichnet den Balken "ENEMY" (im HUD 0x141140A00
nur im Story-Modus für Ziele bestimmter Typen, z. B. 0x2C9) als 20
Vierecke (0x141088D98). Volle Segmente: Tabellenfarbe (0x142514F18) × 1,4,
leere × 1,4 × 0,7, Alpha 0x7F bzw. 0x6E – Werte für die Konsolen-Regel
"0x80 = voll": volles Gelb 177/255, leeres 124/255. Der Vertex-Shader des
Ports (`ts.vp`) multipliziert jede Eckenfarbe mit 255/127; voll und leer
sättigen beide bei 255, nur das Alpha unterscheidet sich (100 % zu 86 %).
Die Xbox-Fassung hat dieselbe Logik und dieselben Konstanten (PAL
0xC8F60, Tabelle 0x2CC300).

**Abhilfe.** Für die Vierecke des Balkens (Aufruf 0x141140906) halbiert
ts2.exe die Farbanteile; das Alpha bleibt (dort entspricht die Verdopplung
der Konsole). Gemessen bei 12 von 20 Segmenten: vorher voll (255,255,0) /
leer (224,75,5), jetzt voll (177,177,0) / leer (114,40,6), wie auf den
Konsolen-Bildern im Issue. Ohne Endgegner erscheint kein Balken.

Test: `TS2_BOSSTEST=<n>` zeichnet im Story-Modus nach dem HUD einen Balken
mit n vollen Segmenten von links (−n: von rechts), `TS2_BALKEN_ALT=1`
zeigt ihn wie vorher. Ohne diese Variablen ändert sich am HUD nichts.

### Bildschirme mit Text (Redux-Issue #23)

**Befund.** Computer-Bildschirme sind Rückrufe eines Objekts (Anmeldung
0x1411F3E50), die ihren Inhalt in Konsolen-Pixeln oben links in den
Bildpuffer zeichnen; 0x14122C5D0 → 0x14122B384 kopiert dann ein Rechteck
in Texturgröße heraus (z. B. Tastenfeld "Enter code:" 0x141160FD4,
128×64). Rechtecke und Linien passen (die Oberfläche des Ports hat
Pixel-Koordinaten 1920×1080), die Schriften sind aber für 1080 Zeilen
gemacht, also 2,25-mal so groß wie auf der Konsole. "Code eingeben:" in
Schrift 4 ist 321 Pixel breit (Textur 128); auf dem Bildschirm steht nur
der Anfang, stark vergrößert. Die Handkarte (nur Linien) ist nicht
betroffen.

**Abhilfe.** Die vier Rückrufe mit Text (0x141153FA8, 0x141160FD4,
0x141161530, 0x141170A68; fünf Anmeldungen mit `lea r8`) gehen über
Sprünge in einer eigenen Seite (RVA 0x48000); für ihre Dauer sind alle 24
Schriftstile (0x14259C020, Maßstab +8/+0xC) um 480/Höhe der Oberfläche
verkleinert. "Code eingeben:" ist dann 142 Pixel breit, TS2 wählt wie auf
der Konsole die schmalere Schrift 0x11, und Text samt Ziffern passen in
die Textur.

**Geprüft** mit `TS2_SCHIRMTEST=1` (zeichnet dieselben Aufrufe wie das
Tastenfeld nach dem HUD, links wie bisher, rechts verkleinert). Offen: ein
echter Bildschirm im Level vor die Kamera bringen (in Siberia-, Chicago-
und Notre-Dame-Start wurde keiner gezeichnet); `TS2_SCHIRM_ALT=1` schaltet
die Verkleinerung für den Vergleich ab. Bei einer Render-Auflösung ungleich
1920×1080 ist die Kopie (echte Pixel) nicht geprüft.

## Launcher-Details

Waffennamen im Launcher (Reiter Waffen): Spielerwaffen über die
Gegenstandstabelle `0x142512A20` (42 Gegenstände à 0x30, +8/+0xC Zeile
linke/rechte Hand; Namen über `0x142513200`, 42 à 0x48, +0 Text-ID);
Doppelwaffen (x2) nutzen die Zeile der einfachen. Ohne Gegenstand:
27 Geschützturm (Levelobjekt Typ 35, `0x1411504D8`), 29 Walzenroboter
(Figur 16 → Klasse 3), 42 Splitter (Figuren 55/161), 43 Drohnen-Splitter
(Figur 128), 32 Zombie-Angriff (Klasse 1, Liste `0x141085568`); Klasse
`[chr+8]` aus `0x141088410`, angeborene Zeile `+0x6B0`/`+0x110` in
`0x1410879C8`. Nur nach ihren Werten benannt: 28, 30, 31, 33, 38, 39, 41, 44.
Ausgeblendet: 18 (unbenutzt), 34 (Digitalkamera), 35 (Temporal-Uplink); ihre
Schlüssel `feuerrate_<zeile>` wirken weiter.

Programmsymbol: `tools/icon/make_icon.py` erzeugt `src/launcher/res/zeitbrecher.ico`
(eigene Zeichnung: gebrochene Uhr); beide EXEs tragen es als RT_GROUP_ICON 1
(`src/launcher/res/launcher.rc`, `src/host/ts2.rc`).

## Fenster und Vollbild

- **DPI:** `ts2.exe` ist DPI-bewusst (pro Monitor, v2) und arbeitet in
  echten Pixeln. Vorher hat Windows es bei Anzeigeskalierung (hier 225 %)
  nachträglich gestreckt.
- **Fenster:** 80 % des Bildschirms im Seitenverhältnis des Bildes.
- **Vollbild:** `[grafik] vollbild=1` oder Alt+Enter schaltet randloses
  Vollbild über den Monitor des Fensters ein und aus. DXGIs eigenes
  Alt+Enter ist abgeschaltet (`DXGI_MWA_NO_ALT_ENTER`). Vorher schaltete es
  ins exklusive Vollbild, ohne dass die Puffer angepasst wurden, und das
  Bild stand.
- **Skalierung:** Das Flip-Modell skaliert das Bild in Render-Auflösung auf
  jede Fenstergröße.
- **Geprüft:** Fenster 3072×1728; Vollbild 3840×2160; Alt+Enter zweimal
  hin und zurück.

## Aus dem Xbox-Fork übernommen (Steuerung, Waffen, Menü)

Grundlage sind die Commits von frankyfife in
[split2-recomp_ff](https://github.com/frankyfife/split2-recomp_ff). Die
Logik ist übertragen, die Adressen sind im PC-Code neu bestimmt und
geprüft.

**Sprung** (`ts2_jump`, Leertaste / rechte Schultertaste)
- Der Sprung betritt Bewegungsmodus 2 mit Aufwärtsgeschwindigkeit, dazu
  wie der Gehcode beim Übergang ins Fallen: Quergeschwindigkeit
  (+0xDF8/+0xE00), Bit 0x800000 in +0x10, Fallhöhe.
- Geprüft im Stand und im Lauf, Landung in Modus 0.

**Rennen** (`ts2_run_walk`, Shift / linke Schultertaste; `[bewegung]`
`rennen`, `renntaste`, `renntempo`, `rennen_umschalten`)
- Der einzige Aufruf des Gehcodes (`0x1411DBE73` → `0x1411D9748`) geht über
  einen Umweg (`hooks.cpp`).
- Beide Schritt-Argumente und die Höchstgeschwindigkeit der Haltung
  (Spieler +0xCC + 4·Haltung, Haltung +0x168; Xbox +0xC0/+0x15C) werden für
  den Aufruf multipliziert.
- Gemessen (`TS2_RENNPROBE=1`): 1,042 → 1,667 Einheiten je 10 Takte, also
  1,6-fach wie im Fork.

**Feuerrate** (`ts2_rof`; `[waffen] feuerrate`, `feuerrate_<zeile>`)
- Waffentabelle `0x14250C2A8`: 46 Zeilen à 0x240, Aufbau wie auf der Xbox,
  Werte der USA-Fassung (17 von 17 Stichproben).
- Die Schussabstände (+0x58 + Modus·0xB0 + 0x10/0x14) werden durch das
  Tempo geteilt, nie unter einen Takt.
- Geprüft: Zeile 0 wird bei 200 % von 30/32 zu 15/16 Takten, wie im Fork.

**Waffenwechsel-Tempo** (`ts2_switch`; `[waffen] wechseltempo`)
- 20/7 bei `0x1421DA4B0`, einziger Leser `0x14112D4A0`.

**Mauszielen**
- **Nickgrenze** (`ts2_pitch_limit`): `0x143F1A390` (Xbox 0x497344) steht
  im Mausmodus auf `[maus] nickgrenze` (85) statt 40, fürs Pad wieder auf
  dem Spielwert.
- **Sichtfeld** (`ts2_fov`): `[grafik] sichtfeld` (70–150 %, Vorgabe 100).
  Die Kamera-Einrichtung `0x14107EE0C(near, far, aspect, fov_grad)` (Xbox:
  camera_setup) baut die Projektion und legt tan(fov/2) und
  aspect·tan(fov/2) für das Ausblenden ab (Kamera `*0x143F3A870`
  +0x348/+0x34C). Beide Aufrufe (`0x141080C85` beim Einrichten,
  `0x14108208A` je Bild mit dem gemerkten fov +0x350) gehen über eine
  Hülle, die das fov skaliert; Projektion und Ausblenden passen so
  zusammen, das Zielfernrohr zoomt im Verhältnis des Spiels. Geprüft:
  Glass-Smash-Start bei 100 und 130 %, breiteres Bild ohne fehlende Teile
  am Rand.
- **Freie Kamera** (`ts2_freecam`): `[kamera] freecam=1`, Taste
  `freecam_taste` (F6) löst die Kamera vom Spieler und bringt sie zurück.
  WASD bewegen, Maus blickt, Leertaste/C steigen/sinken, Umschalt viermal
  schneller (Pad: linker Stick, rechter Stick, Steuerkreuz, linke
  Schultertaste). Der Spieler bekommt solange keine Eingabe außer Pause.
  Kameramodus 8 (`0x143F3A888`) baut die Sicht aus Position `0x143F3A8A0`
  und Blickpunkt `0x143F3A8AC`, die `features_tick` je Takt schreibt. Den
  Raum, aus dem das Level gezeichnet wird, nimmt Modus 8 vom Spieler
  (`0x1410828EE`); ein Stück dort nimmt stattdessen den Raum an der
  Kamera-Position (Raumsuche `0x1411BFA08` wie die Flugkamera Modus 6),
  sonst verschwände das Level, sobald die Kamera den Raum verlässt.
  Skript-Bit `0x08000000` schaltet um (Tests). Geprüft: im Glass-Smash-Start
  über die Mauer geflogen, Level vollständig; danach steht der Spieler wie
  vorher.
- **Räume in der freien Kamera** (`[kamera] freecam_raeume`, Vorgabe 1).
  TS2 zeichnet nur, was die Portale ab dem Kameraraum zeigen: `0x141078598`
  (je Bild, Endsprünge `0x14114C82F`/`0x14114CA1A` nach der
  Kamera-Aktualisierung) flutet über die Portale (`0x141077938`, Seitentest
  `0x1410778DC`, Tiefen `0x141077A08`, Bildrechtecke `0x141077B10`/
  `0x141077EE0`) und legt je Raum r bei Kamera `+0x384+12·r` Tiefe (−1 =
  unsichtbar) und Rechteck ab, `+0xCF0` die größte Tiefe. Der Raumzeichner
  `0x141076C18` (Xbox `FUN_0001DA30`) zeichnet erst die Kulisse (Raum 0xDE,
  ohne Tiefe), dann die Räume mit Tiefe 0…0x4F von vorn nach hinten und
  durchsichtig zurück; Objekte und Figuren hängen über „Raum sichtbar“
  (Kamera `+0xCF4[r]`, `0x141074C80`) daran. Sichtkegel und Far-Plane (1000)
  blenden in der freien Kamera nichts falsch aus. Findet die Raumsuche an
  der Kamera keinen Raum (über oder außerhalb des Levels), bleibt der alte
  Raum, und ganze Teile fehlen. Stufe 1 gibt dann nach jeder Rechnung jedem
  Raum das volle Bild und eine Tiefe nach dem Abstand zu seinem Kasten
  (Räume `*0x142EC3138` à 0xC0, `+0x78` Kasten); Stufe 2 immer, Stufe 0 nie.
  Immer alle Räume zeigt Räume hinter der Kulisse (Siberia: Eiswände durch
  den Schnee), deshalb nur außerhalb. Gemessen (Siberia, 60 Hz): alle Räume
  0,9–1,0 statt 0,3–0,5 ms Zeichenzeit (CPU), 2,4–3,7× Zeichnungen. Geprüft:
  normales Spiel pixelgleich, innen Stufe 1 = Spiel, außen vollständig.
- **Spieler hierher** (`[kamera] teleport_taste`, F7; Pad: Benutzen während
  der freien Kamera; Skript: Bit 0x40000 während der freien Kamera). Sucht von
  der Kamera abwärts den ersten Punkt im Kasten eines nicht flachen Raums mit
  Boden (`0x1411BFB64`, mit Objekten) im Kasten; die Raumsuche allein liefert
  über Schluchten die Auffangfläche unter dem Level (Siberia Raum 9,
  y −51,8). Setzt wie die Startplatzierung `0x1411D6E70` (ohne Startwaffe
  `0x1411D4818`): Fuß auf Boden + 0,01, Auge `0x1411D6DB8`, Vorlagen
  `+0x1A0/+0x1AC`, Gier `+0x154/+0xE70`, Nick `+0x158`, Objekt `+0x1C8`
  (`+0x6C/+0xB4/+0x88/+0x18`), Raum `+0xD78/+0xD7C`, Gehmodus, Fallstart
  `+0x1A20` = 2e38 (kein Fallschaden), dazu den Bodenzustand der Kollision
  (Charakter +0x1ADC Höhe, +0x1AE0 Fläche; gelesen von 0x1411C0E98) wie TS2s
  eigenes Versetzen 0x1411AA40C auf die neue Lage (sonst setzt der Gehcode
  den Fuß auf den alten Boden: Spieler sank in Kisten und Dächer), und
  beendet die freie Kamera. Ohne
  Boden: abgelehnt (Log, Signalton). Fällt der Spieler danach über 1 s oder
  hat keinen Raum, kommt er an die alte Stelle zurück. Geprüft: Siberia
  (läuft, Zaun hält ihn auf, Scanner zeigt die Wachen an der neuen Stelle),
  Challenge Pane in the Neck; außerhalb abgelehnt; Siberia: Wachen greifen
  an, Schüsse treffen; Kiste (1,22) und Dach (3,68) stehen, herunterlaufen
  fällt richtig; Chicago Straße, Haus, Treppe, Obergeschoss. Proben:
  `TS2_FCPOS=x,y,z,gier,nick[;…]` + `TS2_FCSCHRITT`, `TS2_FCPROBE=1`,
  `TS2_TPPROBE=1`.
- **Zurückkippen beim Gehen:** Der Fork hält dafür +0x150 fest. Gemessen
  (`TS2_BLICKPROBE`): Der PC-Port kippt mit der Maus nicht zurück (Nick
  bleibt 30°, Zustand 1), also nichts zu tun.
- **Zielen ohne Taste:** Das war im Fork ein Ausgleich für Stick-Zielen.
  Der PC-Port nimmt die Maus von Haus aus (Port 6); nicht übernommen,
  solange kein Problem belegt ist.

**Menümaus** (`ts2_menu_mouse`; `[maus] menue`; `menumouse.cpp`)
- Jede Liste ist ein Menü, das `0x1410DFE4C(menu)` je Takt aktualisiert
  (Xbox `FUN_000F1D30`); sein einziger Aufruf `0x1410E05AB` geht über einen
  Umweg.
- PC-Aufbau, gemessen; gegenüber dem Xbox-Panel wegen der 8-Byte-Zeiger
  verschoben:

  | Feld | Offset |
  | --- | --- |
  | Seite (beschäftigt: Seite +0x10 & 2) | +0x08 |
  | sichtbar und bereit | +0x14 & 6 |
  | Lage | +0x3C/+0x40 |
  | Größe | +0x4C/+0x50 |
  | Rumpf | +0x2C/+0x30 |
  | Scroll | +0x5C |
  | Einträge | +0x3A0 |
  | Zeilen | +0x3A8 |
  | gewählte Zeile | +0x3B0 |
  | Eintrag: nächster | +8 |
  | Eintrag: Zeilenanfang | +0x28 |
  | Eintrag: Flags (8 nicht wählbar, 0x10 gewählt, 0x100 Rumpf) | +0x44 |
  | Eintrag: Rechteck | +0x9C/+0xA0/+0xA4/+0xA8 |

  Koordinaten im 1920×1080-Raum der Oberfläche.
- **Bedienung:** Der Windows-Zeiger ist im Menü frei. Über einer Zeile
  drückt `ts2.exe` Hoch/Runter, bis TS2 sie gewählt hat; Linksklick = OK,
  Rechtsklick = Zurück. Ein Menü ohne gewählte Zeile (Briefing) nimmt einen
  Klick als OK.
- **Pausenmenü:** Einträge ohne Höhe, Lage als Float +0x98; am Bild
  gemessen y = 571 − 593·v (±6 px).
- **Geprüft per Skript** (`--menuemaus takt:x,y[,l|r];…`):
  - Hauptmenü → Challenge, zweites Set, Rechtsklick zurück;
  - Arcade → Arcade Custom → Tempo;
  - Pausenmenü: Quit/Continue wählen, Continue anklicken.
- **Spur:** `TS2_MENUSPUR=1`.
- **Offen:** Raster mit Spalten (falls es sie gibt; in den geprüften Menüs
  keine), Schieberegler, ein eigener Zeiger.

## Xbox-Vergleich mit xemu (`tools/xemu/`)

`tools/xemu/xemu_skript.py` startet die Xbox-PAL-Fassung in xemu (eigene
Konfiguration und HDD-Kopie unter `game/xemu/`, Fenster 1280×960, Port 1 =
Tastatur; die Konfiguration und Festplatte des Nutzers bleiben unberührt),
steuert sie per Skript (Zeitpunkte in Sekunden, `tippe/halte/lasse`,
Bildvergleich `warte` für Menüs) und schreibt PNG-Serien nach
`game/xemu_ref/`. Zustände werden per QMP `savevm`/`loadvm` gesichert
(`speichere NAME` bzw. `--laden NAME`); `siberia_start` steht am
Spielbeginn in Siberia (Normal). Ab einem Zustand ohne Eingaben sind
Aufnahmen pixelgleich; mit Eingaben kann es ±1 Spielbild (20 ms, PAL 50 Hz)
Versatz geben, deshalb Vergleichsszenen als Zustand sichern und ohne oder
mit kurzen Eingaben aufnehmen. Die PC-Entsprechung ist das Siberia-Skript
mit `--eingabe` (Takt ~4400). Während eines Laufs braucht xemu die
Tastatur. Bedienung: `tools/xemu/README.md`.

## Watchdog (`src/host/watchdog.cpp`)

`ts2.ini` `[debug] watchdog=1` (Launcher: Spiel → Debug-Optionen) oder
`TS2_WACHHUND=1` startet einen Thread, der TS2s Taktzähler (`0x142EE3DE8`,
vom Begrenzer `0x14107CC64` je Bild erhöht) beobachtet. Steht er länger als
`[debug] watchdog_ms` bzw. `TS2_WACHHUND_MS` (Vorgabe 2000), hält der Thread den Spiel-Thread kurz an
und schreibt nach `ts2.log`: RIP, Register, die möglichen Rücksprungadressen
im TS2-Code (rohe Suche über 1024 Stapeleinträge) und eine Häufigkeitsliste
aus 200 RIP-Stichproben (zeigt die Schleife). Höchstens sechs Meldungen je
Stillstand; "Takt läuft wieder", sobald es weitergeht. Ladebildschirme
können ohne echten Hänger anschlagen.

## Robot Factory (Redux-Issues #12/#25, nicht nachgestellt)

Level l_41 (Story-Index 8, Levelnummer 0x29). Die Level-Aktualisierung
`0x14116F78C` (Tabelle `0x14255AAB0`) und der Todesrückruf der Energieknoten
`0x14116F600` ("%d energy nodes remaining", Text 1168/1169) sind wie auf der
Xbox (PAL `0x186440`). Nur die Knoten 0x7B (Normal/Schwer, hinter den
Schilden) und 0x80 (Leicht) öffnen beim Tod das Lasertor 0x7F, schalten den
Laservorhang (Effekt 9, Art 0xF) ab und geben die Wegverbindung der Pads
0x444-0x445 frei; danach soll laut Berichten TS2 hängen ("vorher am Tor
gewesen" = Laservorhang berührt). Der Wachroboter (Pult 0x195, Schalter
`0x1411E0A8C` → `0x1411E0600`, Spieler-Bewegung 8) und der Strahl, der nach
den Schilden 0x7B zerstört (Effekt 7, Art 0x13, Schadensart 0x18), laufen in
ts2.exe; nachgestellt mit Torbesuch, Roboterkamera, Schilden über TS2s
Schadensweg, bei 60 und 120 Hz und auf Leicht: kein Hänger, kein Absturz.
Mögliche Erklärungen: unter Homefront wirkt ein Absturz in TS2 wie ein
Einfrieren (Homefront läuft weiter); oder die Ausräum-Schleife des
Effekt-Ringspeichers (`0x1411AA054`, 256 KB) endet nicht, wenn alle Blöcke
frei sind. Probe: `TS2_RFTEST="takt:befehl;..."` (Befehle in
`src/host/levelfix.h`).

## Bildtakt

TS2 hält seine 60 Takte selbst ein (`0x14107CC64`). Es schläft mit
`Sleep(ms)` bis kurz vor den nächsten Takt und wartet den Rest aktiv ab.
Liegt dabei mehr als ein Takt zwischen zwei Bildern, rechnet es 2–4 Takte
auf einmal (`0x14259CBE8`).

`ts2.exe` lässt TS2 den Takt:
- **Zeitgeber:** `timeBeginPeriod(1)`, und `Sleep` ist durch einen
  hochauflösenden Wartezeitgeber ersetzt.
- **Kein Warten auf den Monitor:** Das Flip-Modell zeigt jedes Bild ab dem
  nächsten Bildwechsel (`Present(0)`). Mit `[grafik] bildsync=1` koppelt
  `ts2.exe` an den Monitor (siehe „Mausgefühl und Latenz“).
- **Grafikkarte:** `ts2.exe` nimmt die leistungsstärkste GPU (DXGI
  "high performance").

`ts2.log` meldet alle 600 Bilder Mittel, Spanne und Ausreißer. Gemessen im
Level auf einem 120-Hz-Monitor: 16,67 ms im Mittel, 16,1–17,2 ms Spanne,
keine Ausreißer; Spitzen nur bei Ladebildschirmen. Vorher lief das Warten
auf den Monitor gegen TS2s eigenen Takt. Mehr als 60 Bilder pro Sekunde
brauchen die Entkopplung von Takt und Bild (Roadmap).

## Mausgefühl und Latenz (`input.cpp`, `timing.cpp`)

**Maus 1:1.** TS2 summiert die Mausbewegung (vtable +0x78/+0x80) in
`0x143F0F4A8`/`0x143F0F4A4`; scePadRead gibt je Lesung die Differenz zur
vorigen (`0x143F0F4AC`/`0x143F0F4B4`) × 0,2 (`0x14259CF44`) weiter. Gemessen
(`TS2_MAUSSKRIPT`, `TS2_MAUSPROBE`): linear, ohne Glättung, Kappung oder
Beschleunigung, Drehung im selben Bild. Aber Gier 0,5333°, Nick nur 0,2667°
je Zählschritt, beides × Bildzeit (`0x14259CBE4`): bei `takt=120` halb so
viel Drehung je Zählschritt, in Bildern mit zwei Takten doppelt. `ts2.exe`
teilt durch die Bildzeit, gibt Nick doppelt und setzt die Summen je Bild auf
den ungelesenen Rest zurück (sonst wachsen sie unbegrenzt, float verliert
Stellen). Jetzt dreht ein Zählschritt immer 0,5333° × `[maus]
empfindlichkeit`, waagerecht wie senkrecht, bei 60 und 120 Hz (geprüft,
auch 0,3 Zählschritte je Bild). Wer bei `takt=120` dasselbe waagerechte
Tempo wie vorher will, setzt `empfindlichkeit` auf 0,5.

**Takt vor der Eingabe.** TS2s Begrenzer (`0x14107CC64`, aus Render) wartete
nach dem Zeichnen bis zur nächsten Periode; zwischen dem Einlesen der Maus
und Present lag so fast eine ganze Periode. Jetzt wartet `ts2.exe` am
Bildanfang (`timing_wait`) und liest danach die Eingabe. Der Begrenzer
bekommt an seinen QPC-Aufrufen (`0x14107CC8C`, `0x14107CDE8`) die
Periodengrenze, sieht ganze Perioden und zählt die Takte wie bisher, ohne
selbst zu warten (`TS2_TAKTALT=1`: altes Verhalten). Die Swapchain hat ein
Waitable Object mit höchstens einem Bild in der Warteschlange; auch auf einen
freien Bildpuffer wird vor der Eingabe gewartet.

| Eingabe bis Present | vorher | nachher |
| --- | --- | --- |
| 60 Hz | 16,6 ms | 0,5 ms |
| 120 Hz | 8,3 ms | 0,7 ms |

**Bildsync** (`[grafik] bildsync=1`): koppelt das Bild an den Monitor, wenn
sein Bildwechsel zum Takt passt (120 Hz bei `takt=120`, 60 Hz bei
`takt=60`): Warten auf den freien Bildpuffer, Present(1), die Takte aus der
vergangenen Zeit gerundet. Ein Bild je Bildwechsel, gemessen 8,25 ms von der
Eingabe bis zur Anzeige, Bildabstand σ 0,001 ms. Passt der Bildwechsel nicht,
wirkt es wie 0. Ohne Bildsync taktet `ts2.exe` nach der Uhr: kürzeste
Latenz; ohne G-Sync/FreeSync kann die Schwebung zwischen Uhr und Monitor
selten ruckeln. Siberia-Skript bei `takt=120`: Bildnummern verdoppeln
(`640:40:10,1200:40:10,1400:100:10,1800:40:10,2200:40:10,2900:40:10,3400:40:10,3800:40:10`,
Level ab Bild ~8600).

## 120 Hz nativ, Stufe 1: Spieler und Kamera

Einschalten mit `[grafik] takt=120` (oder `TS2_TAKT=120`); Vorgabe bleibt
60.

**Was `timing.cpp` macht**
- Der Begrenzer taktet mit 1/120 s.
- Die Zeitfortschreibung bekommt weiter 60-Hz-Takte. Spielzeit und Uhren
  laufen gemessen mit 60 Takten/s.
- Die Integer-Takte wechseln 1/0: Zähler, Schussabstände und KI laufen im
  60-Hz-Raster mit richtigem Tempo.
- Die Spieler-Aktualisierung (`0x14114C990`, Aufrufe `0x1411C6C93` und
  `0x1411C6D99`) bekommt in jedem Bild die halbe Float-Bildzeit. Vorher hat
  die Objektschleife `0x1411FE70C` sie auf ganze Takte zurückgesetzt; so
  lief der Gehcode nur jedes zweite Bild.
- Bewegung, Blick, Sprung und Kamera laufen damit in jedem der 120 Bilder.
- Die übrige Welt (Objekte mit Zeitstempeln) rechnet weiter in 60-Hz-Takten.

**Gemessen** (Siberia, Skript)

| | 60 Hz | 120 Hz |
| --- | --- | --- |
| Gehcode-Aufrufe | jedes Bild, Bildzeit 1,0 | jedes Bild, Bildzeit 0,5 |
| Gehen je 10 Bilder | 1,042 | 0,521 (gleiches Tempo pro Sekunde) |
| Rennen je 10 Bilder | 1,667 | 0,833 |
| Sprung: Flugzeit | ≈ 0,47 s | ≈ 0,50 s (gleich innerhalb der Probenauflösung) |
| Sprung: Scheitel | 1,12 | 1,16 (feinere Schritte, näher am exakten 1,2) |
| Bildzeit | 16,67 ms | 8,33 ms |

- **Kamera:** Zwei aufeinanderfolgende Bilder unterscheiden sich im Lauf in
  über 600 000 Pixeln, sowohl in Bildern mit als auch ohne Takt; im Stand
  nur in rund 1 500 (Schnee). Die Kamera bewegt sich also in jedem Bild.
- **Schuss und Waffenwechsel:** Der Schusscode (`0x1411276E0`) und der
  Waffenwechsel (`0x14112D3FC`) laufen nur bei `Takte ≥ 1`, also
  unverändert im 60-Hz-Raster.
- **Menümaus:** Klicks greifen in Bildern mit und ohne Takt.

**Offen**
- Von Hand prüfen: Waffenschwanken, Kopfwippen und andere Effekte, die pro
  Aufruf statt pro Takt rechnen könnten.
- Gegner und Objekte (Stufe 2: Objektuhr).
- Dämpfungen pro Takt im Dispatcher (`0x1411DBEED`, `0x1411DC352`) laufen
  noch im 60-Hz-Raster.
- Proben: `TS2_GEHPROBE=1`, `TS2_BILDPAAR=1`.

## 120 Hz, Stufe 2 (Gegner und Objekte): Forschungsstand

Nur mit `TS2_STUFE2=1`; Standard bleibt Stufe 1.

**Ansatz:** Der Aufruf `0x1411F3B54(obj, 2, 0)` (Stelle `0x1411FE896`)
verteilt die Aktualisierung an die Klasse des Objekts. `timing.cpp` setzt
dort die Float-Bildzeit pro Objekt auf halbe Takte, gemessen mit einem
eigenen 120-Hz-Zeitstempel pro Objekt; die Integer-Takte bleiben.

**Messung:** `TS2_OBJSUMME=1` misst die Strecke aller Objekte in den
Spielsekunden 34 bis 44 (Siberia, Skript). Die Läufe sind deterministisch.

| Variante | Strecke | Bewegungsschritte |
| --- | --- | --- |
| 60 Hz | 605 | 2 078 |
| 120 Hz, Stufe 1 | 623 (+3 %) | 2 144 |
| Stufe 2, halbe Takte für alle | 545 | 2 145 |
| Stufe 2, Einordnung pro Objekt | 744 | 2 144 |

Die Einordnung pro Objekt funktioniert so: Bewegt sich ein Objekt in
Bildern ohne Takt, läuft es flüssig; sonst bekommt es die Originalwerte.

**Befund**
- Die meisten Objekte bewegen sich auch mit halben Takten nur in Bildern
  mit ganzem Takt. Ihre Klassen sperren also selbst ohne ganzen Takt, mit
  Logik pro Klasse.
- Stufe 1 hält das Tempo der Objekte. Beide Stufe-2-Varianten verfälschen
  es.
- Weiter geht es nur über die Klassen-Aktualisierungen (Tabelle
  `0x142F94FF0`, Klasse × Platz) selbst, Klasse für Klasse mit dieser
  Messung.

## 120 Hz nativ: Befund

TS2 rechnet in ganzen 1/60-s-Takten, und die Logik hängt daran.

**Wie TS2 taktet**
- **Begrenzer** `0x14107CC64`: wartet eine Periode (`0x142EE39D0`) und
  zählt die Takte des Bildes (`0x14259CBE8`, 1–3).
- **Zeitfortschreibung** `0x141222A30`: macht daraus die Bildzeiten
  `0x14259CBD8` (int), `0x14259CBDC` (int, Pause 0), `0x14259CBE0`
  (float), `0x14259CBE4` (float, Pause 0) und zählt die Spielzeit
  (`0x142F9A22C`, ohne Pause `0x142F9A230`).
- **Datensatz pro Spieler und Bild** (`0x1411454EC`): Lockstep, für
  Netzwerkspiel ausgelegt. Gebaut von `0x1411446A8`, zurückübertragen von
  `0x14114435C`. Er enthält Spielzeit, Pad-Daten und die Takte als Bytes
  (+0x28, +0x29); daraus werden die Bildzeiten neu gesetzt.
- **Bild ohne ganzen Takt:** Dann nimmt `0x1411454EC` einen eigenen Zweig
  (Bildzeit 0): Die Logik läuft in diesem Bild nicht.

**Wer die Bildzeiten liest**
- Float: 658 Stellen in 308 Funktionen.
- Integer: 231 Stellen in 130 Funktionen (Zähler, Schussabstände, Timer,
  Zufall pro Takt).
- Davon teilen 7 Stellen ungesichert durch die Integer-Takte.

**Was `ts2.exe` mit `TS2_TAKT=120` macht** (`timing.cpp`)
- Der Begrenzer taktet mit 1/120 s.
- Die Zeitfortschreibung bekommt 60-Hz-Takte; die Spielzeit läuft
  gemessen genau 60 Takte/s.
- Die Float-Bildzeiten werden nach der Zeitfortschreibung und nach dem
  Datensatz-Übertrag auf halbe Takte gesetzt.
- Die 7 Teilungen liefern bei 0 Takten „kein Ereignis“.
- Ergebnis: 8,33 ms pro Bild, stabil.

**Grenze**
- **Integer-Takte im Wechsel 1/0:** Die Logik läuft nur in jedem zweiten
  Bild und das mit halber Bildzeit, also halbes Spieltempo (gemessen 0,22
  statt 0,52 Einheiten pro 10 Bilder).
- **Ein Takt pro Bild:** Dann liefen alle Integer-Zähler doppelt so
  schnell (Schussabstände, KI, Timer).

Weitere Befunde (`tools/re/ticks.py` ordnet alle 306 Stellen in 193
Funktionen ein):

- **Objektzeit:** `0x1411FE70C` aktualisiert die Spielobjekte (Liste
  `0x143F120A0`). Jedes bekommt Takte = Objektuhr (`0x143F2F7A4`) − sein
  Zeitstempel (+0x1C), höchstens 5, und setzt daraus die Integer- und
  Float-Bildzeit pro Objekt neu. Typen mit Flag 0x800 (Tabelle
  `0x14257F424`) bekommen die globalen Takte. Die Spielwelt lebt also in
  ganzen Takten mit Zeitstempeln pro Objekt; Planungen „Uhr + N Takte“
  hängen daran.
- **Abfragen:** 132 Stellen fragen „Takte ≠ 0?“, aber oft als Wächter einer
  Schleife pro Takt (z. B. Dämpfung im Bewegungs-Dispatcher `0x1411DBEED`:
  `do x += x·k while (++i < Takte)`). Pauschal auf die Float-Bildzeit
  umlenken wäre falsch; jede Stelle braucht ihre gebrochene Form
  (x·(1+k)^t).

Echte 120 Hz ohne Zwischenbilder heißen deshalb: die Integer-Takt-Logik
der 130 Funktionen auf halbe Takte umstellen. Das gehört zu „Dekompilieren
durch Ersetzen“ (Roadmap Schritt 4). Bis dahin bleibt der Takt bei 60 Hz;
Stufe 1 (oben) gibt Spieler und Kamera die halben Takte. Proben:
`TS2_TAKTPROBE=1` (Spielzeit je Sekunde), `TS2_RENNPROBE=1` (Strecke).

## Was fehlt (in dieser Reihenfolge)

1. **Bildrate**: Takt und Bild entkoppeln (Tick bei fester Rate, Render frei),
   dann ≥ 120 fps; HDR über die Swapchain.
2. Ein Arcade-Match und eine Challenge durchspielen (bisher nur die Menüs geprüft).
3. Ton am Gerät anhören (bisher nur über Pegel und Mixer-Vergleich geprüft); LFE, Hall.
4. LAN (`ce_lan`, gEnv-Netzwerk), Vollbild, Launcher.
