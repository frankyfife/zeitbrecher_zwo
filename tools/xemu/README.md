# xemu-Werkzeug: Xbox-Referenzbilder von TimeSplitters 2

`xemu_skript.py` startet die Xbox-PAL-Fassung in xemu (Fenster 1280×960), steuert sie per Skript über
die Tastatur und nimmt Bilder (PNG) auf. Zweck: dieselben Szenen wie im PC-Port nachstellen.

## Grundsätzliches
- Nutzerdateien bleiben unberührt: `%APPDATA%\xemu\xemu\xemu.toml` wird nur **gelesen** (BIOS-, Flash-,
  DVD-Pfade). Gearbeitet wird in `game\xemu\` (nicht im Repo): eigene `xemu.toml` (bei jedem Lauf neu
  geschrieben), `hdd_basis.qcow2` (einmalige Kopie der Nutzer-HDD), `eeprom.bin` (Kopie),
  `hdd_lauf.qcow2` (je Lauf frisches Overlay auf `hdd_basis`, per `qemu-img` neben xemu.exe).
- Ausgabe: `game\xemu_ref\<skriptname>\` (oder `--aus`): `b_<sekunden>s.png` (Serie), benannte Bilder,
  `index.tsv`, `protokoll.txt`, `xemu_ausgabe.txt`.
- **Fokus:** Eingaben gehen per SendInput an das vordere Fenster. Das Werkzeug holt xemu vor jeder Taste
  nach vorn und lässt die Taste aus, wenn das nicht gelingt. Während eines Laufs PC nicht bedienen.
  **Esc** (echte Taste) bricht ab. xemu wird immer beendet (QMP `quit`, sonst WM_CLOSE, sonst kill).
  Nie zwei xemu gleichzeitig (das Werkzeug verweigert den Start, wenn schon eins läuft).
- Bilder per PrintWindow (Fallback: Bildschirmausschnitt, nur bei schwarzem Ergebnis); Ton ist aus (`--ton` schaltet ihn ein).

## Tastaturbelegung (Port 1 = Tastatur, Vorgabe von xemu 0.8)
| Xbox | Name im Skript | Taste | | Xbox | Name im Skript | Taste |
|---|---|---|---|---|---|---|
| A / B / X / Y | `A` `B` `X` `Y` | A, B, X, Y (US-Lage; Y = Z auf deutscher Tastatur) | | Linker Stick | `LS_HOCH/RUNTER/LINKS/RECHTS` | E / D / S / F |
| Start / Back | `START` `BACK` | Eingabe / Rücktaste | | Rechter Stick | `RS_HOCH/RUNTER/LINKS/RECHTS` | I / K / J / L |
| Steuerkreuz | `HOCH` `RUNTER` `LINKS` `RECHTS` | Pfeiltasten | | Trigger | `LT` / `RT` | W / O |
| Weiß / Schwarz | `WEISS` `SCHWARZ` | 1 / 2 | | Stickknöpfe | `LS` / `RS` | 3 / 4 |

Sticks sind digital (voller Ausschlag). TS2-Belegung: linker Stick gehen, rechter Stick blicken, RT feuern.

## Aufruf
```
python -I tools/xemu/xemu_skript.py tools/xemu/siberia.txt               # Menüs -> Siberia, Zustand sichern, steuern
python -I tools/xemu/xemu_skript.py tools/xemu/siberia_steuern.txt --laden siberia_start
python -I tools/xemu/xemu_skript.py --laden siberia_start --serie 0:0.5:5 --dauer 5 --aus game/xemu_ref/test
python -I tools/xemu/xemu_skript.py --laden siberia_start --eingabe "1:LS_HOCH:1.5,3:RT" --bilder 0,2,4 --dauer 5
```
Wichtige Schalter: `--laden NAME` (Zustand `game\xemu\zustand_NAME.qcow2`), `--skala N` (interne Auflösung,
Vorgabe 1 = 640×480 wie Xbox), `--fenster BxH`, `--hdd-behalten` (Overlay des letzten Laufs weiterbenutzen),
`--weiter` (bei `warte`-Fehlschlag nicht abbrechen), `--einrichten` (HDD/EEPROM neu kopieren).

## Skriptsprache
Eine Anweisung je Zeile, `#` Kommentar. Zeit in Sekunden ab Start (bzw. ab Laden des Zustands bzw. ab dem
letzten `warte`-Treffer); `+x` = x Sekunden nach der vorigen Zeile.
```
0     serie 0.25 12        # alle 0.25 s ein Bild bis 12 s (serie aus)
0     bild start           # Einzelbild start.png
0.5   halte LS_HOCH        # halten ...
3.5   lasse LS_HOCH        # ... loslassen (lasse alle)
4.5   tippe RT 0.2         # kurz drücken (Vorgabe 0.15 s), mehrere: LS_HOCH+RT
+0.3  warte ref/level.png 100,100,700,160 max=5 frist=60
0     speichere szene1     # Zustand sichern -> game\xemu\zustand_szene1.qcow2
12.5  ende
```
`warte` vergleicht einen Fensterausschnitt (Koordinaten im 1280×960-Fenster) mit einem Vergleichsbild
(mittlerer Grauwertabstand ≤ `max`) und setzt danach die Uhr neu; noch ausstehende Tastendrücke von vor
dem Treffer verfallen. Vergleichsbilder liegen in `game\xemu\ref\` (Spielbilder, daher nicht im Repo):
```
python -I tools/xemu/xemu_skript.py --ausschnitt <bild.png> 100,100,700,160 game/xemu/ref/neu.png
python -I tools/xemu/xemu_skript.py --abstand game/xemu/ref/neu.png 100,100,700,160 <bilder...>
```

## Zustände (Snapshots)
`speichere NAME` ruft QMP `savevm` auf (Maschine ~0.2 s angehalten, wird aus der Zeitachse herausgerechnet);
nach dem Lauf wird das Overlay als `zustand_NAME.qcow2` abgelegt. `--laden NAME` kopiert es nach
`hdd_lauf.qcow2`, startet xemu normal und lädt nach 3 s per QMP `loadvm` (`-loadvm` auf der Befehlszeile
geht nicht: der Tastatur-Controller wird erst nach dem Start angesteckt). Der Zustand bleibt unverändert.
Vorhanden: `siberia_start` (Siberia, Normal, Spielbeginn im Tunnel, Uplink in der Hand).

## Menüfolge Xbox-PAL (deutsche Konsolensprache)
Titel (1 Spieler) → START → Controller wählen → A → Profil: Neuer Spieler → A → Namen eingeben (Vorschlag
zufällig, ENDE vorgewählt) → A → Festplatte → A → Profil wird gespeichert → Hauptmenü (Story vorgewählt) → A →
Story-Vorspann (über 60 s, mit A überspringbar) → Level wählen (Sibirien) → A → Schwierigkeit (Normal) → A →
Einsatzbesprechung → A → Laden → Zwischensequenz (mit A überspringbar) → Spiel. Dauer mit `siberia.txt` ≈ 43 s.

## Grenzen
- Zeitbasis ist die Wanduhr; xemu läuft nicht taktgenau. Die Menüs sind mit `warte` abgesichert; ohne `warte`
  schwanken Ladezeiten/Übergänge um 1–3 s zwischen Läufen.
- Ab einem Zustand **ohne Eingaben** sind die Bilder pixelgleich (gemessen: 0,1-s-Serie zweimal identisch).
- **Mit Eingaben** gelegentlich ±1 Spielbild (20 ms, PAL 50 Hz) Versatz pro Tastendruck: oft identisch,
  manchmal laufen Blickwinkel/Weg nach dem ersten gehaltenen Stick auseinander (mittlere Abweichung bis
  ~40 Grauwerte nach 10 s). Für genaue Vergleiche: Szene einmal ansteuern, mit `speichere` sichern und
  danach ab dem Zustand ohne (oder mit nur kurzen) Eingaben aufnehmen.
- Keine Mausbewegung, Sticks nur digital.
