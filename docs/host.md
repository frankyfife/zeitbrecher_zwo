# ts2.exe: TimeSplitters 2 ohne Homefront

Stand 2026-11-09: `ts2.exe` startet TS2 in einem eigenen Fenster mit
eigenem D3D11-Gerät, ohne Homefront, CryEngine oder Steam, und ist
spielbar: Menüs, Intro, Briefing und das Level Chicago laufen mit Tastatur
und Maus oder einem Pad; der Sprung funktioniert. Arcade, Challenge, alle
Story-Level und das Optionsmenü sind freigeschaltet. Ton und Musik fehlen noch
(siehe unten).

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
Einstellungen in `ts2.ini` neben der EXE (wird beim ersten Start angelegt):
Render-Auflösung, Vollbild, Waffenwechsel-Tempo, Sprung, Tasten, Freischaltungen. Protokoll:
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
| 0x141C34420, 0x141C33B50, 0x141C32C80, 0x141C33A80, 0x141C33FF0, 0x141C32B60 | libvorbisfile (`ov_open_callbacks`, `ov_info`, `ov_pcm_total`, `ov_time_*`, `ov_read`, `ov_clear`), statisch gelinkt | **noch Platzhalter** (Öffnen schlägt fehl) |
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
| +0xD0 | Soundsystem | +0x100(callback) → Objekt, darauf +0x10 (Lautstärke) | Attrappe, liefert ein Attrappen-Objekt |
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

Vorgaben: WASD, Maus zielt (Raw Input, eingefangen, solange das Fenster
vorn ist), linke/rechte/mittlere Maustaste Feuer/Alt-Feuer/Zoom, E benutzen,
R nachladen, Strg ducken, Mausrad Waffe, Esc Pause, Enter/Rücktaste OK/zurück,
Pfeiltasten Menü, Leertaste Sprung (ts2.exe). Pad (XInput): A/B/X/Y =
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

## Was fehlt (in dieser Reihenfolge)

1. **Ton**: die Soundsystem-Schnittstelle (+0xD0) mit XAudio2 (5.1) nachbauen.
2. **Musik/Sprache**: die sechs vorbisfile-Funktionen mit stb_vorbis.
3. **Bildrate**: Takt und Bild entkoppeln (Tick bei fester Rate, Render frei),
   dann ≥ 120 fps; HDR über die Swapchain.
4. Ein Arcade-Match und eine Challenge durchspielen (bisher nur die Menüs geprüft).
5. LAN (`ce_lan`, gEnv-Netzwerk), Vollbild, Launcher.
