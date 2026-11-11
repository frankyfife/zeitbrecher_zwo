# Hochaufgelöste Texturen

Wie man für Zeitbrecher Zwo eigene, höher aufgelöste Texturen macht und
einspielt. Werkzeug: `tools/hd/make_hd.py` (Python mit Pillow und numpy).

**Nie committen:** Originaltexturen und alles, was daraus erzeugt ist
(`game/hd/`, `game/hd_eigen/`, `game/hd_backup/`, Vergleichsbilder), sind
Daten des Spiels. `game/` steht in `.gitignore`. Ins Repo gehören nur die
Skripte, Modellparameter und diese Beschreibung.

## Format

- TS2 lädt Texturen als **SGI-Bilder** (`.rgb`, Magic `01 DA`), 8 Bit je
  Kanal, RLE-gepackt oder unkomprimiert; beides geht. Pillow liest SGI und
  schreibt es unkomprimiert.
- **Kanäle wie im Original:** RGB bleibt RGB, RGBA bleibt RGBA. Weiße
  HUD-Formen (Fadenkreuze, Bögen) stecken nur im Alphakanal (RGB = 255);
  `crosshair_arrow` ist dagegen deckend (Alpha 255) und trägt die Form als
  Graustufe auf Schwarz.
- **Größe:** gleiches Seitenverhältnis wie das Original, ganzzahlig
  vergrößert, möglichst Zweierpotenzen (das Original von `ts2loading` ist
  512×384, also geht auch das). Das Spiel skaliert über die
  Texturkoordinaten; die Bildschirmgröße bleibt gleich.
- **Bildname im Kopf leer lassen.** Das SGI-Namensfeld (Byte 24–103) wird
  beim Laden in einen kleinen Puffer kopiert: Pillow schreibt dort den
  Dateinamen hinein, und mit `gmod_monkey_assist` (18 Zeichen) stürzte
  ts2.exe im Hauptmenü ab (Lesefehler an `0x1411A9CB6`). Die Originale
  lassen das Feld leer, Photoshop schreibt „Photoshop image“ (15 Zeichen,
  geht). `make_hd.py` nullt das Feld nach dem Speichern.
- **Speicher ist knapp.** TS2 hält Texturen in festem Speicher. Mit den
  Lebensbögen in 1024×512 (4fach) bricht der Start von Siberia mit
  `exit(1)` ab, in 512×256 (wie Redux) läuft er. Daher in `make_hd.py`
  Obergrenzen je Ziel (`SEITE`): Fadenkreuze 128, Bögen und `ts2banner1b`
  512, sonst 4fach bis höchstens 1024. Nach dem Vergrößern weiterer
  Texturen immer einen Levelstart prüfen (siehe unten).

## Wie ts2.exe sie findet

Gibt es `game/hd/ts2/`, liest ts2.exe jede Datei zuerst unter `game/hd/`
mit demselben relativen Pfad, dann erst das Original (Log: „Texturen:
hochaufgelöste aus game/hd zuerst“, je Datei `[datei] rb hd/...`). Beispiel:
`game/hd/ts2/textures/misc/crosshair_circle.rgb` ersetzt
`game/ts2/textures/misc/crosshair_circle.rgb`. Nummerierte Dateien wie
`2464.rgb` sind Leveltexturen, `misc/` enthält HUD und Menü.

Ordner unter `game/`:

| Ordner | Inhalt |
|---|---|
| `ts2/textures/` | Originale (nur lesen) |
| `hd/` | was das Spiel lädt (anfangs die Texturen von TS2 Redux, `tools/re/hd_textures.py`) |
| `hd_eigen/` | eigene Fassungen von `make_hd.py`, zum Vergleichen |
| `hd_backup/` | von `--installieren` ersetzte Dateien aus `hd/` (einmalig gesichert) |

## Eigene Texturen machen

### 1. Prozedural (einfache HUD-Formen)

Fadenkreuze und Lebensbögen sind einfache Formen. `make_hd.py` zeichnet sie
mit Abstandsfeldern neu (Strecken, Kreisbögen, Ringe, Ellipsen) und
kantenglättet sie in der Zielauflösung; so bleiben sie bei jeder Größe
scharf. Die Maße sind am Original gemessen: `--anpassen` verändert die
Parameter (Radien, Linienbreiten, Bogenlängen, Deckkraft, Schein), bis das
auf Originalgröße verkleinerte Bild dem Original möglichst gleicht, und gibt
sie aus; `--messen` zeigt die verbleibende Abweichung (RMS, derzeit 4–7 von
255). Die `mess`-Werte (Lage im Original, z. B. ist `crosshair_olde` um ein
halbes Pixel versetzt) gelten nur beim Messen, die Ausgabe ist mittig.

Neue Form: Modellfunktion `m_<name>(L, p)` schreiben, die Deckung 0..1
liefert, in `VEKTOR` mit Startwerten eintragen, `--anpassen --nur <name>`
laufen lassen und die Werte übernehmen.

### 2. Vergrößern (fotoartige Texturen)

Kiste, Riffelblech, Zaun, Baum, Spielmodus-Symbole, Banner, Ladebild:
Lanczos-Vergrößerung je Kanal, danach eine leichte Unschärfemaske. Der
Alphakanal wird getrennt vergrößert; bei Ausschnitt-Texturen (Zaun, Baum,
Symbole: `alpha='scharf'`) wird die Kante auf etwa 1,5 Ausgabepixel
aufgesteilt. Vorher wird die Farbe deckender Pixel in durchsichtige
ausgebreitet, damit keine dunklen Säume entstehen. Kachelnde Texturen
(`kachel=True`: Riffelblech, Zaun) werden vor dem Vergrößern umlaufend
aufgefüllt, damit die Ränder nahtlos bleiben; die Kiste ist eine
Seitenfläche und kachelt nicht. Das macht Texturen glatter, aber nicht
detailreicher: Für Kiste, Riffelblech und Zaun sind die handgemachten
Redux-Fassungen deutlich besser.

### 3. Von Hand (Bildbearbeitung)

Für alles, was mehr Details braucht: Original als Vorlage öffnen
(`python -I tools/hd/make_hd.py --liste "misc/gmod_*"` zeigt Größe und
Kanäle), in 2-, 4- oder 8facher Größe neu zeichnen, dann als SGI speichern:

- GIMP: „Exportieren als“ → `.rgb`/`.sgi`, Kompression „RLE“ oder keine.
- Photoshop: Format „SGI RGB“; Alphakanal bei RGBA-Texturen mitspeichern.
- Oder als PNG speichern und mit Pillow umwandeln (dabei den Bildnamen
  leeren, siehe `sgi_speichern` in `make_hd.py`).

Kanäle wie im Original, Seitenverhältnis gleich, Bildname leer oder kurz.
Die Datei kommt nach `game/hd_eigen/<gleicher Pfad>` und wird wie unten
installiert. Eigene Quelldateien (PSD, SVG, XCF) dürfen ins Repo, wenn sie
nichts aus dem Spiel enthalten (also neu gezeichnet, nicht abgepaust oder
übermalt).

## Aufrufe

```
python -I tools/hd/make_hd.py                         # alle Ziele nach game/hd_eigen
python -I tools/hd/make_hd.py --nur "crosshair_*" 2464
python -I tools/hd/make_hd.py --liste "misc/crosshair*"  # Originale: Größe, Kanäle, hd/eigen vorhanden
python -I tools/hd/make_hd.py --vergleich <Ordner>    # vergleich_<gruppe>.png: Original | Redux | eigen
python -I tools/hd/make_hd.py --installieren [--nur ...]   # hd_eigen -> hd, Ersetztes nach hd_backup
python -I tools/hd/make_hd.py --zuruecksetzen [--nur ...]  # hd aus hd_backup wiederherstellen
python -I tools/hd/make_hd.py --messen | --anpassen [--nur ...]
```

Die Vergleichsbilder zeigen alle drei Fassungen bilinear auf dieselbe Größe
gebracht (wie das Spiel filtert); „Redux“ kommt aus `hd_backup/`, falls dort
gesichert, sonst aus `hd/`. Vergleichsbilder nicht ins Repo legen.

### Im Spiel prüfen

In einer Kopie von `ts2.exe` und `ts2.ini` außerhalb von `build\bin`
(Kopie z. B. mit `vollbild=0`, `hdr=0`):

```
TS2_TAKT=60 ./ts2.exe --spiel "D:/Projekte/Timesplitters 2/game" --takte 600 --bild menue.bmp
TS2_TAKT=60 ./ts2.exe --spiel ".../game" --takte 2400 --bild level.bmp \
    --eingabe 320:40:5,440:40:5,560:40:5,680:40:5,800:40:5,920:40:5,1040:40:5,1160:40:5,1280:40:5,1400:40:5,1520:40:5,1640:40:5
```

Das erste lädt das Hauptmenü (dabei werden Symbole, Banner und Ladebild
gelesen), das zweite drückt OK durch Story, Intro und Levelwahl bis in
Siberia (Fadenkreuze, Bögen, Kiste, Tanne, Zaun werden geladen). Im Log
nach `rb hd/` (geladen), `[absturz]` und `exit(1)` sehen.

## Stand

| Ziel | Verfahren | Größe | Urteil gegen Redux |
|---|---|---|---|
| `misc/crosshair_{arrow,circle,old,olde,shaded,triangle}` | prozedural | 128² | gleichwertig, näher am Original (Redux hat Formen vereinfacht) |
| `misc/crosshair_laser` | vergrößert | 128² | etwa gleich |
| `misc/arc_bar`, `misc/arc_outline` | prozedural (Ellipsen) | 512×256 | näher am Original; Redux schließt die Kontur oben rund ab, das Original blendet aus |
| `misc/ts2smallbanner1-4`, `ts2banner1b`, `ts2loading` | vergrößert | 2fach | gleich (Redux: ebenfalls vergrößert bzw. Original) |
| `misc/gmod_escort` | vergrößert | 256² | besser (Redux: Original 64²) |
| übrige `misc/gmod_*` | vergrößert | 256² | etwas weicher als Redux |
| `2475` Tanne | vergrößert | 512×256 | wie das Original; Redux ersetzt ihn durch einen dichteren Busch |
| `2416` Riffelblech, `2464` Kiste, `2476` Zaun | vergrößert | 4fach | schlechter: Redux ist neu gezeichnet |

Offen: Kiste, Riffelblech, Zaun und die Symbole von Hand neu zeichnen;
weitere HUD-Texturen ohne Redux-Fassung (`crosshair_drop`, `arc_back`,
`arc_glow`, …) als Modelle anlegen; die Fadenkreuze im Spiel bei gezogener
Waffe ansehen.

## Dank

[TS2 Redux](https://github.com/HFTSRedux/TS2Redux) (HD-Textures) hat
gezeigt, welche Texturen sich lohnen und welche Größen das Spiel verträgt.
Deren Quelldateien werden hier nicht verwendet; die eigenen Fassungen sind
aus den Originalen gemessen bzw. vergrößert.

## Schriften

TS2 zeichnet Text mit drei Schrift-Texturen: `misc/impact` (256², HUD,
Waffennamen, Ziffern, Menütitel), `misc/book` (128×256, Menüs, Untertitel)
und `misc/booklight` (128×192). Sie sind weiß, die Form steckt im Alpha.
`tools/hd/make_fonts.py` zeichnet sie je Zeichen nach (Zellen aus den
Zeichentabellen in `game/ts2.bin`): Es schätzt eine glatte, harte Form,
deren Kantenglättung auf Originalgröße das Original ergibt, mittelt
spiegelsymmetrische Zeichen (A, H, O, V …) an ihrer besten Achse, verfolgt
die Kante (Marching Squares) und ordnet sie: optimale Zerlegung in Geraden
und Kreisbögen, abgerundete Ecken werden spitz, runde Strichenden gerade
Kappen, fast achsparallele Kanten exakt waagerecht/senkrecht; schriftweit
rasten Grundlinie/Versal-/x-Höhe und gleich dicke Striche auf gemeinsame
Werte ein. Ein Sicherheitsnetz lässt Zeichen, die geordnet deutlich
schlechter passen, nur geglättet (booklight `7`, `|`); `--svg --einfach`
nimmt für alle das frühere, nur glättende Verfahren. Die Texturen werden
auf die Zeichenzellen beschnitten, weil das Spiel beim Filtern über den
Zellrand hinaus liest (sonst Saum). Die Umrisse stehen als SVG in
`game/hd_eigen/schrift/<name>.svg` (bearbeitbar, Ebene „vorlage“ zeigt das
Original); daraus werden 4fache Texturen mit derselben Zeichenanordnung
(`hd_eigen/ts2/textures/misc/`). `--messen` zeigt die Abweichung und die
Zeichen mit der größten (RMS 3,1/4,6/5,3 von 255 für impact/book/booklight;
geglättet ohne Ordnen 2,6/4,5/5,2 – der Unterschied ist die bewusste
Angleichung, vor allem das Spiegeln bei impact), `--vergleich <Ordner>`
Original und neu nebeneinander, `--installieren` kopiert nach `game/hd`. Die
Zeichentabellen im Programm zählen in Pixeln des Originals; ts2.exe rechnet
sie auf die geladene Größe um (`src/host/fontfix.cpp`); ohne diese
Umrechnung zeigt eine größere Schrift-Textur nur ihr oberes linkes Viertel.
SVGs und Texturen sind aus Spieldaten abgeleitet: nie committen.

```
python -I tools/hd/make_fonts.py
python -I tools/hd/make_fonts.py --installieren
```

## Schriftzug im Hauptmenü (`tools/hd/logo.py`)

Das Hauptmenü (0x1410B5A8C) zeichnet misc/ts2newlogo (Schriftzug, Form im
Alpha) und darüber misc/ts2glow (Schein, RGB = Alpha, additiv; formt auch
das brennende Logo), beide 512×256 RGBA, als Viereck x 40..1880,
y −48..492 (1840×540, 3,41:1; Textur 2:1, waagerecht 1,70fach gestreckt,
oberste 48 Zeilen außerhalb). Keine HD-Fassung (Redux ohne Logo).
TS2 nimmt die Größe aus der Datei; 2048×1024 und 1840×540 (keine
Zweierpotenz) sind geprüft. TS2s .rgb stehen für Pillow auf dem Kopf.

```
python -I tools/hd/logo.py --export        # PNGs + README nach game/hd_eigen/logo
python -I tools/hd/logo.py --installieren neu.png --schein [--groesse 1840x540]
python -I tools/hd/logo.py --zuruecksetzen --schein
```

`--schein` erzeugt ts2glow aus dem neuen Alpha (Gauß, gegen das Original
RMS 13/255), damit Menü-Schein und Feuer zum neuen Schriftzug passen.

ts2glow formt auch das brennende Logo im ersten Bildschirm. Soll das so
bleiben, erzeugt `--menueschein` statt `--schein` einen eigenen Schein nur
für das Hauptmenü (`game/hd/ts2/textures/misc/ts2glow_menue.rgba`, Format
"TS2R" + Breite/Höhe + RGBA8, oberste Zeile zuerst): ts2.exe
(`src/host/menulogo.cpp`) bindet ihn für die beiden ts2glow-Zeichnungen des
Hauptmenüs (Schein 0x1410B5C76; Alpha-Maske für Feuer und dunklen Hof in
0x1410B59A8, Aufruf 0x1410B5A55). Fehlt die Datei, bleibt alles wie bisher.
`--lage X0,Y0,X1,Y1` streckt den Umriss des Schriftzugs auf ein Viereck im
1920×1080-Bild (`original` = Lage des Original-Schriftzugs 112,51–1772,359;
das Original erscheint im Spiel 1,70fach waagerecht gestreckt).
`--relief STÄRKE` gibt ihm ein leichtes Relief wie dem Original: Licht von
oben links auf die weich gerundete Form, dunklere Innenkante, Verlauf von
hell zu graublau. Liegt in game/hd noch die frühere eigene Fassung, wird sie
nicht nach hd_backup gesichert (`--zuruecksetzen` holt so das Original).
Eigener Schriftzug des Nutzers (1500×527):

```
python -I tools/hd/logo.py --installieren game/hd_eigen/logo/neu_schriftzug_vorlage.png --lage 112,40,1772,390 --relief 0.6 --menueschein
```

Breite wie das Original, 1,70fach gestreckt wie das Original.
