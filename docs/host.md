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
umgestellt; die Lauftasten bleiben dabei, wie sie waren. Pad (XInput): A/B/X/Y =
Kreuz/Kreis/Quadrat/Dreieck, rechter/linker Trigger = R1/L1, Schultertasten =
R2/L2, Start/Back = Start, Steuerkreuz. Alles in `ts2.ini` ([tasten], [maus])
änderbar. Es gilt das zuletzt benutzte Gerät.

Testskripte ohne Bildschirm: `--eingabe takt:bits:anzahl,...` (Bits
hexadezimal; zusätzlich `80000000` Sprung, `40000000` vorwärts) mit
`--takte` und `--bild`. Geprüft: 320:40:5 wählt im Titel "Story", weitere
OKs überspringen das Intro und führen über Levelwahl und Briefing nach
Chicago; Sprung 0 → Modus 2 (+0,145) → Landung Modus 0.

Offen: die Pad-Belegung für das Spiel selbst (welche Konsolenbits TS2 im
Spiel für Feuern/Zielen nutzt) ist am Pad noch nicht ausprobiert.

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
(Vorgabe: Story, Arcade, Challenge an; Cheats, Unverwundbarkeit, alles aus)
und trägt mit `optionen=1` einen eigenen Aufbau des Hauptmenüs in die
Seitenbeschreibung ein (`0x1424F2DB0`, wie TS2Redux): Story, Arcade,
Challenge, Options, Quit, mit den Maßen und Texten des Originals.

Geprüft per Skript: Arcade (League/Custom), Challenge (Challenge-Sets),
Options (Spieler, Ton, Abspann, Zwischensequenzen, Cheats) öffnen sich,
"Zurück" führt ins Hauptmenü; die Story-Levelwahl zeigt mehr als zwei
Level.

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

## Fehlerbehebungen nach TS2 Redux (`src/host/fixes.cpp`, `fixes.inc`)

[TS2 Redux](https://github.com/HFTSRedux/TS2Redux) (GPLv3, Fanoto,
DevilDwarf und andere) behebt Fehler des TS2-Ports mit DLLs, die beim Start
Bytes in Homefronts EXE schreiben. Diese Behebungen stehen jetzt als Tabelle
in unserem Code und sind immer an; ts2.exe braucht dafür weder Redux noch
dessen Dateien:

| Gruppe | Inhalt |
|---|---|
| spielfehler | Reaper-Spawn-Absturz (`0x1411004E4`), Regen auf der Kamera (`0x14122A7AE`, ein `int3`) |
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

## Arcade und Challenge im Spiel

Geprüft per Menümaus-Skript (`--menuemaus`, Koordinaten aus `TS2_MENUSPUR`):

- **Arcade Custom:** Hauptmenü → Arcade → Arcade Custom → Tempo „Normal“,
  dann die Vorgaben mit OK. Das Match auf `l_42_AR` läuft mit Rangliste,
  Bots auf dem Radar und Waffen, ohne Absturz.
- **Challenge:** Hauptmenü → Challenge → „Glass Smash“, erste Challenge
  (`l_35_AR`). Die Spieluhr zeigt nach 35,8 s Spielzeit 00:36,1 bei 60 Hz
  und 00:36,2 bei 120 Hz. Die Spielzeit läuft also auch bei 120 Hz richtig.

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

## Schatten (`src/host/shadowfix.cpp`, Kontext-Hülle `ctxproxy.h`)

Kisten warfen im PC-Port schwarze Flecken (Xbox: schwache, weiche
Abdunklung). Befund mit `TS2_DRAWDUMP=<takt>` (protokolliert jede Zeichnung
mit gewöhnlicher Alpha-Mischung, schreibt ihre Texturen roh heraus;
`TS2_DRAWSKIP=<textur>`, `TS2_DRAWSKIPN=<nummern>`, `TS2_DRAWSHADER=<textur>`
mit Pixel-Shader, Eingabe-Layout und Ecken):

- Der Fleck ist eine 16×16-Textur (schwarz, weiches Quadrat in Alpha bis
  0,62), gezeichnet mit SRC_ALPHA/INV_SRC_ALPHA; der Pixel-Shader rechnet
  Textur × Eckenfarbe (COLOR0, RGBA8 bei Abstand 40).
- In den Ecken des Kistenschattens stehen an 32..47 aber vier Floats (eine
  Weltlage); die Deckkraft ist so das oberste Byte eines Floats (0x42 ≈ 0,26
  bis 0xBF ≈ 0,75, je nach Lage), und der Fleck wird zweimal gezeichnet:
  fast schwarz. Andere Flecken (Figuren) haben richtige Eckenfarben.
- Abhilfe: Für Zeichnungen mit dieser Textur (am Inhalt erkannt) setzt
  ts2.exe einen eigenen Pixel-Shader mit fester Deckkraft
  (`[grafik] schatten`, Vorgabe 0,35; 0 = aus). Geprüft an der Kiste im
  Siberia-Tunnel und draußen im Schnee.
- Die D3D11-Laufzeit schreibt die Funktionstabelle ihres Kontexts selbst um;
  umgelenkte Kontext-Methoden hielten nicht (auch der frühere Befund "TS2
  löscht nie" war deshalb falsch: es löscht auf 0,0,0,0). Deshalb bekommt TS2
  eine Hülle um den Kontext (`ctxproxy.h`, Weiterleitungen aus d3d11.h
  erzeugt mit `tools/gen_ctxproxy.py`).

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

## Bildtakt

TS2 hält seine 60 Takte selbst ein (`0x14107CC64`). Es schläft mit
`Sleep(ms)` bis kurz vor den nächsten Takt und wartet den Rest aktiv ab.
Liegt dabei mehr als ein Takt zwischen zwei Bildern, rechnet es 2–4 Takte
auf einmal (`0x14259CBE8`).

`ts2.exe` lässt TS2 den Takt:
- **Zeitgeber:** `timeBeginPeriod(1)`, und `Sleep` ist durch einen
  hochauflösenden Wartezeitgeber ersetzt.
- **Kein Warten auf den Monitor:** Das Flip-Modell zeigt jedes Bild ab dem
  nächsten Bildwechsel (`Present(0)`). Mit `[grafik] bildsync=1` wartet
  `ts2.exe` zusätzlich auf den Monitor (wie bisher).
- **Grafikkarte:** `ts2.exe` nimmt die leistungsstärkste GPU (DXGI
  "high performance").

`ts2.log` meldet alle 600 Bilder Mittel, Spanne und Ausreißer. Gemessen im
Level auf einem 120-Hz-Monitor: 16,67 ms im Mittel, 16,1–17,2 ms Spanne,
keine Ausreißer; Spitzen nur bei Ladebildschirmen. Vorher lief das Warten
auf den Monitor gegen TS2s eigenen Takt. Mehr als 60 Bilder pro Sekunde
brauchen die Entkopplung von Takt und Bild (Roadmap).

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
