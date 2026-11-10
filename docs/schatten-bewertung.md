# Echte Schatten im PC-Port: Bewertung

Stand 2026-11-10. Frage: Lassen sich in `ts2.exe` echte Schatten ergänzen,
innen von Lampen, draußen von Sonne/Himmel? Was geht, was kostet es, was
kann schiefgehen?

**Kurzfassung:** Ja, es ist machbar. Die Voraussetzungen sind besser als
erwartet. TS2 zeichnet die ganze Welt mit **einem** Vertex-/Pixel-Shader-Paar
(`ts.vp`/`ts.pp`). Die Sichtmatrix liegt getrennt in der Kamera-Struktur,
das Level kommt in Weltkoordinaten (`mv` = Sicht). Die Lampen des Levels
stehen mit Weltposition, Richtung und Reichweite in einer Liste im
Speicher. Was fehlt: eine Sonnenrichtung (TS2 kennt kein gerichtetes
Licht) und ein lesbarer Tiefenpuffer (D24S8 ohne SRV-Bindung). Außerdem
steckt das Licht des Levels vorberechnet in den Eckenfarben; jeder neue
Schatten muss sich damit vertragen.
Empfohlene Reihenfolge: (1) Tiefenpuffer lesbar machen + SSAO, (2)
Klecksschatten von der nächsten Lampe weg strecken, (3) Schattenkarten
nur für bewegliche Werfer (Figuren, Kisten) von Lampen und einer
Sonne je Level aus einer Tabelle.

Teil 1 enthält nur Gemessenes, Teil 2 und 3 sind Schätzungen.

---

## 1. Messungen

### Messaufbau

- Eigener Build `build-schatten` aus einer Kopie von `src/` im Temp-Ordner
  (Repo unverändert). Probe `TS2_ALLDRAWS=<takt>` (zusammen mit
  `TS2_DRAWDUMP=<takt>`): protokolliert in einem Bild **jede** Zeichnung
  (Topologie, Shader, Eingabe-Layout, Blend/Tiefe/Stencil/Cull, RT/DSV mit
  Format, Texturen, Vertexpuffer), alle VS-Konstantenpuffer roh, die ersten
  Ecken, die Disassembly jedes Vertex- und Pixel-Shaders, die
  Kamera-Struktur und die Lichtliste. Zeichnungen je Takt alle 50 Takte.
- Siberia mit dem Skript aus der Einweisung, `TS2_TAKT=60`:
  - **Tunnel:** Takt 4600, Spieler steht im Tunnel (mit Kartengerät in der
    Hand, bzw. mit Fäusten nach Waffenwechsel).
  - **Draußen:** freie Kamera (Bit `0x08000000` bei 4450, vorwärts ab 4470),
    Takt 4650: Zaun, Bäume, Gebäude, Schneefall.
- Grafikkarte: RTX 5090; 1920×1080.

### Zeichnungen je Bild

| Szene | Zeichnungen | davon Level | Objekte (Figuren, Kisten, Waffen) | Partikel/Sprites | 2D/HUD |
| --- | --- | --- | --- | --- | --- |
| Tunnel, Kartengerät | 693 | 138 (≈26 100 Indizes) | ≈240 (mit Ego-Gerät) | 12 | ≈300 |
| Tunnel, Fäuste | 413 | – | – | – | – |
| Draußen, Schnee | 1 184 (zweiter Lauf 1 149) | 143 (≈26 700 Indizes) | ≈300 (mit Ego-Gerät) | 441 (427 Schneeflocken à 6 Indizes) | ≈310 |

Verlauf Siberia (Takte 3700–4600): 667–701 je Bild. Menüs bis 2 056. Pro
Bild zeichnet TS2 also einige zehntausend Dreiecke; für eine heutige GPU
ist das nichts. Die Last liegt in der Zahl der Aufrufe (D3D11, ein Thread).

Nur `DrawIndexed`/`Draw` mit Dreiecksliste (4) und -streifen (5) für die
Welt; Linien (2) und Punkte (1) im 2D-Teil. Kein Instancing, kein Stencil
(in allen 1 562 daraufhin geprüften Zeichnungen, Tunnel 413 + draußen 1 149, `StencilEnable=0`), Culling überall
aus (`CULL_NONE`), Tiefentest `LESS`.

### Shader: ein Paar für alles

In beiden Szenen nutzt **jede** Zeichnung denselben Vertex- und Pixel-Shader
(aus `ts2/data/shaders/ts.vp`/`ts.pp`, kompiliert, DXBC; `tso.*`/`tsx.*`
liegen daneben, wurden in Siberia nicht geladen). Konstantenpuffer:

| Slot | Name | Inhalt (gemessen) |
| --- | --- | --- |
| cb0 | `PER_INSTANCE` (112 B) | `mv` (4×4), `ambient`, `spec` (Flags, uint), `alphaRefMin/Max`, `bias` (z-Versatz in Sichtraum), `detailUVScale` |
| cb1 | `PER_FRAME` (64 B) | `proj` |
| cb2 | `MATRICES` (1 600 B) | 25 Matrizen (Knochen), Index aus `TEXCOORD2` |
| cb3 | `LIGHTS` (144 B) | `lightpos[3]`, `lightcol[3]`, `falloffs[3]` |

Konvention Zeilenvektor (`p · M`). Der VS rechnet `p_sicht = p · mv`
(oder · Knochenmatrix), dann `· proj`. Bits in `spec`, aus der Disassembly:

| Bit | Wirkung | wo |
| --- | --- | --- |
| 0x1 / 0x2 | Eckenfarbe BGRA/RGBA (sonst 0,5), ×2,008 (PS2: 0x80 = 1,0) | VS |
| 0x4 (oder 0x01000000) | Textur 0 | PS |
| 0x20 | keine Projektion (2D) | VS |
| 0x40 / 0x100 | Knochen aus cb2 statt `mv` (Haut) | VS |
| 0x200 | 3 Punktlichter je Ecke (Sichtraum, Abfall min/max) | VS |
| 0x400 | 3 gerichtete Lichter (Richtung in `lightpos`) | VS |
| 0x2000 | Detailtextur (t1) | PS |
| 0x10000 | Sprite-Versatz aus `TEXCOORD1` in Sichtraum | VS |
| 0x01000000 | UV aus der Normalen (Umgebung) | VS |
| 0x20000000 | 3 Punktlichter je Pixel | PS |
| 0x10, 0x80, 0x8000 | im Shader ungenutzt | – |

Gemessene Flag-Gruppen (Tunnel und draußen):

| `spec` | Art | Beispiel |
| --- | --- | --- |
| `0x20008086`, `0x2000A086` | **Level** (Eckenfarbe, Textur, ggf. Detail, Pixel-Licht) | 143 Zeichnungen draußen; `mv` = Sicht bei allen 143 (im Tunnel 130 von 131 der Level-Gruppe) |
| `0x8284` | starre Objekte mit Lampenlicht (Kisten, Teile von Figuren) | `ambient` 0,31/0,33/0,375 im Tunnel, 0,6 draußen |
| `0x8304` | Haut-Teile von Figuren (Knochen + Lampenlicht) | Knochen in cb2 im **Sichtraum** |
| `0x8016` | Objekte ohne Licht (Ego-Gerät, Bäume/Zaun-Teile, Glas) | |
| `0x18006` | Sprites (Schnee, Funken) | 427 draußen |
| `0x21`, `0x25`, `0x2D` | 2D | |

Der Pixel-Shader ist kurz (47 Befehle): Textur × Eckenfarbe, optional
Detail, optional Pixel-Licht, Alpha-Test über `alphaRefMin/Max` mit `discard`.

### Matrizen: Welt ist rekonstruierbar

- Je Zeichnung gibt es nur `mv` (Modell·Sicht) und getrennt `proj`; eine
  Weltmatrix wird nicht übergeben.
- Die **Kamera-Struktur** `*0x143F3A870` hält alles Nötige (gemessen im
  Tunnel):

| Versatz | Inhalt |
| --- | --- |
| +0x000 | Projektion (= cb1) |
| +0x040 | Kopie der Projektion |
| +0x080 | **Sicht** (Welt → Kamera), gleich `mv` aller Level-Zeichnungen |
| +0x0C0 | fast gleiche zweite Sicht (vermutlich Vorbild/Logiktakt) |
| +0x100 / +0x140 | Sicht·Projektion (und zweite) |
| +0x180 | **inverse Sicht** (Kamera → Welt; Zeile 3 = Kamerapos.) |
| +0x1C0 | zweite inverse Sicht |
| +0x208 | Zeiger auf die Struktur selbst (Ziel von camera_setup `0x14107EE0C` → `0x141220A44`) |
| +0x2A0 | Kameraposition |

- Daraus: `Modell = mv · Sicht⁻¹` je Zeichnung, Knochen ebenso
  (`Knochen_Welt = Knochen_Sicht · Sicht⁻¹`). Geprüft: die Positionen der
  Kisten im Tunnel (−20,96/0/−40,03, −7,35/0/−27,65) liegen auf dem Boden
  (y = 0). Figuren und Lichter passen zueinander.
- **Projektion** (Tunnel, Sichtfeld 100 %): `0,9747 / 1,732` (60° vertikal,
  16:9), `m22 = −1`, `m32 = −0,15`, `m23 = −1`: near 0,15, far sehr groß,
  Tiefe 0 vorn bis 1 hinten (nicht umgekehrt). Ansichtsfenster `MinDepth 0,01`.
- **Level vs. dynamisch** sicher unterscheidbar: Level = `mv` gleich
  Sicht (auf 4 Stellen verglichen), Bit 0x20000000, eigener statischer Vertexpuffer;
  Objekte = `mv` ≠ Sicht, Bits 0x200/0x100.

### Vertexpuffer

- 48 Byte je Ecke wie bekannt. Level und Objekte liegen in **dauerhaften
  Puffern je Netz** (draußen gut 100 verschiedene VBs für ≈400 Zeichnungen);
  nur 2D, Sprites und Partikel kommen aus einem gemeinsamen Ringpuffer.
  Man kann Level-Zeichnungen (VB, IB, Start, Anzahl) also merken und später
  erneut zeichnen.
- Das Level wird je Raum gezeichnet (Portale): Pro Bild kommt nur, was
  sichtbar ist.

### Tiefenpuffer

- Ein DSV 1920×1080, Format 45 (`D24_UNORM_S8_UINT`), Bindung 0x40
  (nur `DEPTH_STENCIL`): **nicht als Textur lesbar**. TS2 legt ihn über
  `CreateTexture2D` an, das `ts2.exe` schon umlenkt (`probe_create_tex2d`).
- Das Render-Target (Format 28, RGBA8) hat Bindung RT|SRV.

### Zeichenreihenfolge (draußen, Takt 4650)

| Zeichnungen | Inhalt |
| --- | --- |
| 0–284 | 2D (Kartenbildschirm), danach Kopie des Bildpuffers als Textur |
| 285–582 | **undurchsichtige Welt**: Level und Objekte je Raum gemischt, Tiefe schreiben |
| 583–708 | durchsichtige Welt (Glas, Zaun, Klecksschatten), Tiefe nur lesen |
| 709–1 116 | Sprites/Schnee |
| 1 117–1 133 | Ego-Modell (Hand, Gerät) |
| 1 134–1 183 | HUD, Bildschirmeffekte (Textur = Kopie des Bildpuffers) |

Der Übergang undurchsichtig → durchsichtig (erste Zeichnung mit
SRC_ALPHA-Mischung nach Welt-Zeichnungen) ist eine brauchbare
Einfügestelle für Bildschirm-Verfahren.

### Licht

- **LIGHTS (cb3)** kommt nur bei Objekten mit 0x200/0x8304 gefüllt an,
  Beispiel (Figur, Tunnel):
  `lightpos = (12,57 1,831 −39,61) (12,57 5,192 −39,58) (0 0 0)`,
  `lightcol = (0,367 0,406 0,48) ×2, 0`, `falloffs = (2 10 0,125) ×2`.
  Positionen im **Sichtraum** (die Figur steht ≈39 Einheiten vor der Kamera).
  Abfall: voll bis 2, null ab 10, Steigung 1/8.
- Draußen ein Objekt mit einer Lampe: Sichtraum (26,44 1,31 −28,89) →
  **Welt (4,8 3,12 4,2)**.
- **Lichtliste des Levels**: `0x143F1A5C0`, Einträge à 0x28, Anzahl in
  `0x142F56340` (Siberia: **128**, womöglich die Obergrenze). Eintrag:
  +0 Weltposition, +0x10 Zeiger auf Lampendaten, +0x18 Zeiger auf das Objekt.
  In den Lampendaten: Lichtform (Mittelpunkt + Rechteck, vermutlich für den
  Lichthof), **+0x80 Richtung** (meist 0/−1/0, also nach unten; an Wänden
  0/0/−1), +0x98 Reichweite min (2), +0x9C Reichweite max (10).
  Beispiele: Lampen an Gebäuden in y 3,12 und 6,48; ein Turm mit Lampen in
  y 4,2/16,3/28,4/40,5.
- **Auswahl je Objekt**: `0x1412233AC` (gerufen aus `0x141223AD0`) geht
  die Liste durch, nimmt Lampen in Reichweite (Abstand² gegen
  Reichweite², Kegel über +0x94), sortiert und gibt **höchstens 3** weiter;
  Zwischenliste ab `0x142F9A2B0` (Farbe, Weltposition, Abstand², Gewicht).
- **Kein gerichtetes Licht:** Bit 0x400 kommt in keiner Messung vor (vier Läufe,
  ≈3 400 Zeichnungen), auch draußen nicht. Eine Sonnenrichtung gibt es im
  Renderer nicht. Der Xbox-Fork kennt für Siberia einen Sonnenschein-Effekt
  (große additive Sprites an einer Sonnenposition; Fork
  `docs/pc-work-plan.md` Abschnitt 13); dessen PC-Adresse ist nicht bestimmt.
- **Level-Geometrie** hat ihr Licht vorberechnet in COLOR0 (`ambient` 0). Ihr
  Pixel-Licht-Pfad (0x20000000) war in allen gemessenen Bildern leer
  (cb3 = 0). Er vergleicht die Lichtposition mit `SV_POSITION`
  (Bildschirmkoordinaten). Ob TS2 dort Bildschirmkoordinaten liefert
  (Mündungsfeuer, Explosionen) oder das ein Fehler des Ports ist, ist offen.

### Klecksschatten und Xbox-Vorlage

- Figuren und Kisten werfen nur Flecken (16×16-Textur, Mischung SRC_ALPHA,
  nach der undurchsichtigen Welt, Flags `0x8284`/`0x8016`). Abhilfe für
  den schwarzen Kistenfleck schon in `shadowfix.cpp`.
- TS2 selbst (PS2/Xbox/GC) hat keine weitere Schattentechnik: kein Stencil,
  keine Schattenkarte. Im Xbox-Fork (`split2`, `src/recomp_manual.c`,
  `docs/`) gibt es nichts zu Schatten außer dem Wort im
  Bildschirmfoto-Werkzeug. Die Strings „Shadow“ in der EXE gehören zu
  CryEngine/Homefront, nicht zu TS2 (keine Verweise aus dem TS2-Block).

---

## 2. Ansätze (Schätzungen)

Gemeinsame Bausteine, die mehrere Ansätze brauchen:

- **B1 Tiefe lesbar:** In `probe_create_tex2d` den D24S8 als
  `R24G8_TYPELESS` mit `DEPTH_STENCIL|SHADER_RESOURCE` anlegen,
  `CreateDepthStencilView` umlenken (Format `D24_UNORM_S8_UINT` eintragen),
  eigenes SRV `R24_UNORM_X8_TYPELESS`. Aufwand ½–1 Tag. Risiko gering
  (gleiches Format im Speicher).
- **B2 Einfügestelle:** In der Kontext-Hülle den Übergang zur ersten
  durchsichtigen Welt-Zeichnung erkennen und dort eigene Durchgänge
  einschieben (DSV kurz lösen, als SRV lesen, wieder binden). ½–1 Tag.
- **B3 Mitschnitt:** Je Zeichnung VB/IB/Start/Anzahl, `spec`, `mv`,
  Knochen und Alpha-Textur merken; Welt aus der Kamera-Struktur. 1–2 Tage.
- **B4 Eigener Pixel-Shader für die Welt:** `ts.pp` ist kurz und einzig;
  als HLSL nachbauen plus Schattenterm, per Zeichnung setzen wie
  `shadowfix.cpp`. Die Weltposition des Pixels ergibt sich aus
  `SV_POSITION` (xy + Tiefe) und den Inversen aus der Kamera-Struktur; der VS
  muss nicht geändert werden. 1–2 Tage.

### (a) Schattenkarten

**a1 Sonne draußen (gerichtet, Kaskaden).** Welt mitschneiden (B3), aus
Sonnensicht 2–3 Kaskaden à 2048² zeichnen (eigener Tiefen-VS, Alpha-Test für
Bäume/Zaun), im Welt-PS (B4) abfragen (PCF). Sonnenrichtung je Level aus
einer Tabelle in der INI oder aus der Position des Sonnenschein-Effekts.
- Nutzen: hoch draußen (Figuren, Kisten, Gebäude, Bäume werfen Schatten in
  den Schnee). Ändert aber den Stil deutlich: das Level ist hell vorberechnet;
  Schatten von Gebäuden sind neu und können gegen die gebackenen Farben
  arbeiten.
- Aufwand: 6–10 Tage bis spielbar, +3–5 Tage Feinarbeit je Levelart.

**a2 Lampen innen.** Lampen zeigen nach unten (+0x80) und reichen 10
Einheiten: je Lampe **eine** Perspektivkarte nach unten (≈150°) oder
Paraboloid statt Würfelkarte. Nur die 1–4 Lampen nahe der Kamera
schattieren. Abdunkeln: den geschätzten Anteil dieser Lampe (Farbe ×
Abfall × N·L, alles bekannt) im Schatten **abziehen**, nicht das ganze
Pixel multiplizieren. So bleibt das gebackene Grundlicht, und es wird nur
dort dunkler, wo diese Lampe hinscheint.
- Nutzen: mittel bis hoch in Gebäuden mit Lampen; im Tunnel (keine Lampe)
  nichts.
- Aufwand: +4–6 Tage auf a1 (Bausteine geteilt).

**a3 Nur bewegliche Werfer** (Figuren, Kisten, Türen) statt auch
Level→Level: Das Level hat seine Selbstverschattung (soweit vorhanden) schon
in den Eckenfarben. Werfer nur aus `mv ≠ Sicht`-Zeichnungen, Empfänger
alles. Das vermeidet Doppelverdunklung und die meisten Lücken (siehe
Risiken) und ersetzt die Klecksschatten dieser Objekte. Für a1 und a2 der
empfohlene Anfang.

### (b) Bildschirm-Verfahren

**b1 SSAO** (B1 + B2): halbe Auflösung, Normale aus der Tiefe,
bilaterale Weichzeichnung, multiplikativ auf das Bild vor der
durchsichtigen Welt; Ego-Modell und HUD kommen später und bleiben
unberührt.
- Nutzen: mittel. Ecken, Fugen, Figuren und Kisten werden geerdet, innen
  wie außen, ohne Lichtdaten.
- Aufwand: 2–4 Tage. Leistung: geschätzt < 0,5 ms bei 1080p, ≈ 1 ms in 4K
  auf der 5090; bei 120 Hz (8,3 ms) unkritisch.
- Risiko: doppelte Verdunklung in schon dunklen, gebackenen Ecken (Stärke
  regelbar, nach Helligkeit dämpfen). Schnee/Sprites schreiben keine Tiefe
  und bleiben ohne AO.

**b2 Kontaktschatten** (Bildschirm-Strahlen zur Lampe): Die Lampen sind
bekannt (Lichtliste), also kurze Strahlen im Tiefenpuffer zur nächsten Lampe
bzw. Sonne. Kleine, scharfe Schatten an Füßen und Kisten. +1–2 Tage auf b1;
gibt aber nur, was im Bild ist (Halos an Bildrändern).

### (c) Klecksschatten verbessern

- Deckkraft nach Höhe über dem Boden, weicherer/größerer Fleck (HD-Textur),
  und den Fleck **von der nächsten Lampe weg strecken** (Lampenposition aus
  der Lichtliste, Objektposition aus `mv`). Das Muster von `shadowfix.cpp`
  reicht (eigener PS/VS-Anteil für die erkannte Textur).
- Nutzen: klein bis mittel, wirkt aber „richtig TS2“. Aufwand 1–2 Tage.
  Risiko gering.

### (d) Schattentechnik der Xbox-Fassung

Gibt es nicht (siehe oben). Nichts zu übernehmen.

### Übersicht

| Ansatz | Aufwand (Tage) | Nutzen | Risiko | Leistung bei 120 Hz |
| --- | --- | --- | --- | --- |
| B1+B2 Tiefe + Einfügestelle | 1–2 | Grundlage | gering | – |
| (b1) SSAO | 2–4 | mittel | gering–mittel (Doppelverdunklung) | < 1 ms |
| (b2) Kontaktschatten | +1–2 | klein–mittel | mittel (Ränder) | < 0,5 ms |
| (c) Klecksschatten | 1–2 | klein–mittel | gering | 0 |
| (a3+a1) Sonne, nur bewegliche Werfer | 6–10 | hoch draußen | mittel | 0,5–1,5 ms CPU (≈200–400 zusätzliche Aufrufe je Kaskade) |
| (a3+a2) Lampen, nur bewegliche Werfer | +4–6 | mittel–hoch innen | mittel | je Lampe ≈ 50–150 Aufrufe |
| (a1) Sonne inkl. Level→Level | +3–5 | hoch, Stilbruch möglich | hoch | wie oben |

Zusammen (B1–B4, b1, c, a3+a1+a2) etwa **3–5 Wochen** für eine
abschaltbare, je Level eingestellte Fassung.

---

## 3. Risiken (Schätzungen)

1. **Gebackenes Licht.** Die Eckenfarben enthalten Lampenlicht (und
   vielleicht Schatten). Multiplikative Schatten verdunkeln doppelt.
   Abhilfe: nur bewegliche Werfer (a3), Lampenanteil abziehen statt
   multiplizieren (a2), Stärke je Level in der INI.
2. **Sichtbare Räume.** TS2 zeichnet nur sichtbare Räume. Werfer außerhalb
   (Säule hinter der Kamera) fehlen in der Schattenkarte. Abhilfe:
   statische Level-Zeichnungen über Bilder hinweg sammeln (Puffer bleiben
   bestehen, Weltkoordinaten); bei a3 kaum relevant.
3. **Ein Bild Verzug.** Mit Mitschnitt aus dem Vorbild hinkt der Schatten
   beweglicher Figuren ein Bild hinterher (bei 120 Hz 8 ms). Alternative:
   Durchgang an der ersten Welt-Zeichnung mit den Objekten des Vorbilds,
   aber aktuellen Kameramatrizen; Figuren ziehen dann leicht nach.
4. **Keine Sonnenrichtung.** Je Level von Hand (Tabelle) oder aus dem
   Sonnenschein-Effekt (Adresse erst zu finden). Levels ohne Sonne
   (Nacht, innen) brauchen keine.
5. **Alpha-Test.** Bäume, Zäune und Gitter brauchen den Alpha-Test im
   Schatten-Durchgang (Textur 0 + `alphaRefMin/Max` aus cb0 vorhanden).
6. **Klecksschatten doppelt.** Für Objekte mit echtem Schatten die Flecken
   ausblenden (Textur am Inhalt erkennen, wie `shadowfix.cpp`); der
   Zusammenhang Fleck → Objekt ist noch zu klären (Lage im Bild genügt
   wohl).
7. **Pixel-Licht-Pfad 0x20000000** ist ungeklärt (Bildschirm- statt
   Sichtraum). Ein eigener Welt-PS (B4) muss ihn genau so nachbilden, sonst
   ändern sich Mündungsfeuer/Explosionen.
8. **Leistung.** Die GPU-Last ist gering. Der Engpass sind D3D11-Aufrufe
   auf einem Thread: 2–3 Kaskaden + 2–4 Lampen verdoppeln bis verdreifachen
   die ≈400 Welt-Zeichnungen. Statische Teile der Karten zwischenspeichern
   (Sonne: nur bei Bewegung neu; Lampen: Level-Anteil einmal). Gemessen ist
   hier nichts: Bei `TS2_TAKT=60` taktet das Spiel das Bild, so dass die
   reine Zeichenzeit verdeckt ist (Zeitstempel-Abfragen nötig).

---

## 4. Empfehlung

1. **B1 + B2** (Tiefe lesbar, Einfügestelle): kleine, nützliche Grundlage,
   auch für spätere Effekte (Nebel, Tiefenschärfe). 1–2 Tage.
2. **SSAO (b1)** mit Regler `[grafik] ao`: größter Gewinn je Aufwand, keine
   Lichtdaten nötig. 2–4 Tage.
3. **Klecksschatten (c)**: Höhe, Weichheit, Streckung von der Lampe weg.
   1–2 Tage, ganz im Stil des Spiels.
4. **Schattenkarten für bewegliche Werfer (a3)**: zuerst Sonne draußen
   (Siberia als Prüfstein, Richtung von Hand), dann die nächsten Lampen
   innen; Level→Level nur als Option. 2–3 Wochen.
5. Optional Kontaktschatten (b2).

Jeder Schritt einzeln abschaltbar, Vorgabe aus, bis er in mehreren Levels
(Siberia, Chicago, ein Innenlevel mit vielen Lampen) geprüft ist.

## 5. Offen

- Bedeutung von `spec`-Bits 0x10/0x80/0x8000 und des Pixel-Licht-Pfads
  (Probe mit Mündungsfeuer/Explosion).
- Lampenfarbe in den Lampendaten (nur in der Zwischenliste gesehen) und
  ob 128 die Obergrenze der Lichtliste ist.
- Sonnenrichtung: Adresse des Sonnenschein-Effekts im PC-Code.
- Andere Levels (Chicago, Innenlevel) mit derselben Probe nachmessen;
  `tso`/`tsx`-Shader: wann geladen.
- Zeichenzeit ohne Taktung (GPU-Zeitstempel).

## Anhang: Probe nachbauen

Der Probe-Code (nicht im Repo) ergänzt `draw_probe` in `main.cpp` um
`frame_probe`: `CreateVertexShader` (Geräte-Methode 12) umlenken und
Bytecode merken; je Zeichnung `VSGetShader`/`PSGetShader`,
`IAGetPrimitiveTopology`, `OMGetDepthStencilState`, `RSGetState`,
`OMGetRenderTargets`, `PSGetShaderResources`, `VSGetConstantBuffers`
(0–13, Inhalt über Staging-Kopie wie `read_buffer`), `log_vertices`; beim
ersten Aufruf Kamera `*0x143F3A870` (+0…+0x380) und Lichtliste
`0x143F1A5C0`/`0x142F56340`. Aufruf:

```
TS2_TAKT=60 TS2_DRAWDUMP=4650 TS2_ALLDRAWS=4650 ./ts2.exe --spiel ".../game" --takte 4652 --bild shot.bmp \
  --eingabe "320:40:5,600:40:5,700:100:5,900:40:5,1100:40:5,1450:40:5,1700:40:5,1900:40:5,4450:8000000:2,4470:40000000:300"
```

(`[kamera] freecam=1` in der Test-INI; ohne die letzten zwei Einträge und
mit Takt 4600 die Tunnel-Messung.)
