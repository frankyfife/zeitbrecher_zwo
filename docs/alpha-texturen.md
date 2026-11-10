# Alpha der Texturen: PC gegen Xbox

Stand 2026-11-10. Frage: Haben PC-Texturen ein anderes Alpha als die Xbox
(deckend statt durchsichtig, umgekehrt, fehlend), und erklärt das die falschen
Schatten unter Kisten, Tischen und Stühlen?

## Vorgehen

`tools/re/xbox_alpha.py <xbox.iso> <game> [--tsv <datei>] [--bin game/xbox_alpha.bin]`
liest alle Paks (`data/**/*.pak`) aus dem eigenen Xbox-Image, dekodiert von
jeder `textures/**/*.xbt` die oberste Stufe und vergleicht ihr Alpha mit
`game/ts2/textures/**/*.rgb` und, falls vorhanden, `game/hd/ts2/textures`
(TS2-Redux-Texturen, die ts2.exe zuerst liest).

- `.xbt`-Kopf: Breite, Höhe, Breite2, Höhe2, Stufen−1 (0 = keine), Art; Daten
  ab 0x80, oberste Stufe zuerst. Art 0 = DXT1 (3375 Texturen), 1 = DXT3
  (1160), 2 = A8R8G8B8, Xbox-swizzled (192). (Die frühere Liste
  `crit.txt` hielt das fünfte Feld für das Format; es ist die Stufenzahl.)
- Zeilen: Die Xbox-Zeile 0 ist die letzte Zeile, die Pillow aus der SGI-Datei
  liest, und die erste, die ts2.exe hochlädt (Inhalts-Hash wie
  `TS2_DRAWDUMP`).
- Manche PC-Texturen liegen gedreht oder gespiegelt vor (64×128 statt
  128×64); das Werkzeug sucht die Lage, in der die Farbe (bzw. das
  PC-Alpha) am besten passt, und vergleicht erst dann.
- Wesentlich heißt: PC deckend (Alpha überall 255), Xbox mit mindestens 1 %
  durchsichtiger Fläche, oder mittlere Abweichung ≥ 16 (von 255).
  Unterschiede nur durch Auffüllen (z. B. 32×24 auf 32×32) zählen nicht.

## Ergebnis

4727 Xbox-Texturen, 20 ohne PC-Datei. 35 Unterschiede:

- **25 deckend auf dem PC, durchsichtig auf der Xbox, plus 2 fast deckende
  (1460, 1529, PC-Alpha 225–255).** Alle sind Leuchttexturen
  (Mündungsfeuer, Lampenschein, Funken, Laser): Die PC-Farbe ist die
  Xbox-Farbe mal Xbox-Alpha (mittlere Abweichung 1–16 von 255), das Alpha selbst ging
  verloren. Mit gewöhnlicher Mischung (SRC_ALPHA/INV_SRC_ALPHA) deckt ihr
  Schwarz ab; mit Addition fällt es nicht auf.
- **Keine umgekehrten Texturen**, keine fehlenden Alphakanäle sonst.
- **Schattentexturen sind gleich.** Alle schwarzen Texturen mit Alpha-Form
  (68 im PC-Satz: 16×16-Klecks 1847/2420/3857, Kistenumriss 1760, Fleck
  unter Fässern 1875, Tisch/Stuhl 32×32 mit Alpha bis 168, Einschusslöcher,
  Brandflecken, Fußspuren, Zielfernrohrrand ...) haben auf PC und Xbox
  dasselbe Alpha. Die Schattenfehler kommen nicht aus den Texturen, sondern
  vom Eckenalpha (docs/host.md "Schatten").
- Rest: HD-Fassungen von TS2 Redux (2475, Spielmodus-Symbole) mit etwas
  anderem Alpha (neu gezeichnet), ein GameCube-Mauszeiger
  (`misc/mm_cursor54_gx`, PC-Alpha 253), `misc/xbbuttons` (leicht anders),
  `misc/mm_cursor40` (PC hat Alpha, Xbox DXT1 ohne).

Spalten: Textur (`textures/<name>.rgb`), Art, Größe Xbox und PC, Xbox-Format
und PC-Modus, Anteil durchsichtig (Alpha < 128) auf Xbox und PC, mittlere
Abweichung, Korrektur in ts2.exe (Tabelle = `game/xbox_alpha.bin`,
Schätzung = Alpha aus der Helligkeit ohne Tabelle), Paks mit der Textur
(`l_20_st` Chicago, `l_35_st` Siberia, `gun` Waffen, `*_ar` Arcade, Anzahl in
Klammern).

| Textur | Art | Xbox | PC | Format | Xbox durchs. | PC durchs. | Abw. | Korrektur | Paks |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1442 | deckend | 64x64 | 64x64 | DXT3 RGBA | 77% | 0% | 193 | Tabelle | gun, l_100, l_10_ar, l_11_ar, l_11_st ... (16) |

| 1443 | deckend | 128x64 | 128x64 | DXT3 RGBA | 75% | 0% | 190 | Tabelle | gun, l_100, l_11_ar, l_11_st, l_13_ar ... (16) |

| 1465 | deckend | 64x64 | 64x64 | DXT3 RGBA | 66% | 0% | 170 | Tabelle | gun, l_10_ar, l_10_st, l_11_ar, l_11_st ... (16) |

| 1487 | deckend | 64x32 | 64x32 | DXT3 RGBA | 62% | 0% | 157 | Tabelle | gun, l_10_ar, l_10_st, l_11_ar, l_11_st ... (16) |

| 1491 | deckend | 128x64 | 64x128 | DXT3 RGBA | 52% | 0% | 131 | Tabelle | gun, l_41_st, l_46_ar, l_64_ar, l_65_ar |

| 1516 | deckend | 64x64 | 64x64 | DXT3 RGBA | 92% | 0% | 225 | Tabelle | gun, l_11_ar, l_11_st, l_31_st, l_32_ar ... (12) |

| 1530 | deckend | 64x64 | 64x64 | DXT3 RGBA | 70% | 0% | 173 | Tabelle | gun, l_10_ar, l_33_ar, l_41_st, l_46_ar ... (10) |

| 1531 | deckend | 64x64 | 64x64 | DXT3 RGB | 65% | 0% | 147 | Tabelle | gun, l_10_ar, l_33_ar, l_41_st, l_46_ar ... (10) |

| 1532 | deckend | 64x64 | 64x64 | DXT3 RGB | 61% | 0% | 146 | Tabelle | gun, l_10_ar, l_33_ar, l_41_st, l_46_ar ... (10) |

| 1534 | deckend | 64x64 | 64x64 | DXT3 RGBA | 40% | 0% | 110 | Tabelle | gun, l_10_ar, l_11_ar, l_32_ar, l_34_ar ... (14) |

| 1535 | deckend | 64x64 | 64x64 | DXT3 RGBA | 82% | 0% | 200 | Tabelle | gun, l_10_ar, l_11_ar, l_32_ar, l_34_ar ... (14) |

| 1539 | deckend | 64x64 | 64x64 | DXT3 RGBA | 86% | 0% | 215 | Tabelle | gun, l_10_ar, l_11_ar, l_32_ar, l_34_ar ... (15) |

| 1574 | deckend | 64x64 | 64x64 | DXT3 RGBA | 59% | 0% | 148 | Tabelle | gun, l_20_st, l_21_ar, l_29_ar, l_43_ar ... (7) |

| 1579 | deckend | 64x64 | 64x64 | DXT3 RGBA | 86% | 0% | 214 | Tabelle | gun, l_10_ar, l_10_st, l_13_ar, l_20_st ... (14) |

| 1610 | deckend | 64x64 | 64x64 | DXT3 RGBA | 86% | 0% | 214 | Tabelle | gun, l_10_st, l_20_st, l_21_ar, l_29_ar ... (11) |

| 1615 | deckend | 64x32 | 64x32 | DXT3 RGBA | 62% | 0% | 157 | Tabelle | gun, l_10_st, l_20_st, l_21_ar, l_29_ar ... (11) |

| 1727 | deckend | 64x64 | 64x64 | DXT3 RGBA | 61% | 0% | 152 | Tabelle | l_20_st |

| 1728 | deckend | 64x32 | 64x32 | DXT3 RGBA | 64% | 0% | 160 | Tabelle | l_20_st |

| 2172 | deckend | 64x64 | 64x64 | DXT3 RGBA | 82% | 0% | 200 | Tabelle | l_31_st, l_38_st |

| 2452 | deckend | 32x32 | 32x32 | DXT3 RGBA | 97% | 0% | 227 | Tabelle | l_35_ar, l_35_st |

| 2543 | deckend | 64x64 | 64x64 | DXT3 RGBA | 83% | 0% | 206 | Tabelle | l_100, l_31_st, l_33_ar, l_35_st, l_40_st ... (6) |

| 3037 | deckend | 64x32 | 64x32 | DXT3 RGBA | 62% | 0% | 157 | Tabelle | l_41_st |

| 3038 | deckend | 64x64 | 64x64 | DXT3 RGBA | 86% | 0% | 214 | Tabelle | l_41_st |

| 3862 | deckend | 64x64 | 64x64 | DXT3 RGBA | 82% | 0% | 200 | Tabelle | l_33_ar, l_55_ar |

| 5211 | deckend | 64x32 | 64x32 | DXT3 RGBA | 45% | 0% | 114 | Tabelle | gun, l_10_ar, l_38_st, l_40_st, l_41_st ... (10) |

| 1460 | anders | 64x64 | 64x64 | DXT3 RGBA | 61% | 0% | 153 | Tabelle | l_40_st |

| 1529 | anders | 64x64 | 64x64 | DXT3 RGBA | 66% | 0% | 163 | Tabelle | gun, l_10_ar, l_33_ar, l_41_st, l_46_ar ... (10) |

| 2475 (HD) | anders | 128x64 | 512x256 | DXT3 RGBA | 77% | 58% | 69 | – | l_35_ar, l_35_st |

| misc/gmod_elimination (HD) | anders | 64x64 | 256x256 | DXT3 RGBA | 44% | 50% | 21 | – | l_101 |

| misc/gmod_leech (HD) | anders | 64x64 | 256x256 | DXT3 RGBA | 49% | 53% | 18 | – | l_101 |

| misc/gmod_regeneration (HD) | anders | 64x64 | 256x256 | DXT3 RGBA | 43% | 46% | 18 | – | l_101 |

| misc/gmod_vampire (HD) | anders | 64x64 | 256x256 | DXT3 RGBA | 36% | 42% | 20 | – | l_101 |

| misc/mm_cursor54_gx | anders | 16x16 | 16x16 | DXT3 RGBA | 89% | 0% | 225 | – | l_102 |

| misc/xbbuttons | anders | 128x64 | 128x64 | DXT3 RGBA | 59% | 59% | 18 | – | l_100, l_101, l_102, l_103, l_104 ... (17) |

| misc/mm_cursor40 | pc-durchsichtig | 32x32 | 64x64 | DXT1 RGBA | 0% | 44% | 123 | – | l_102 |


`misc/xbnullchar` (Schriftzeichen, PC 17×32, Xbox 32×32) ist deckend auf dem
PC; die Schätzung erfasst sie, sie erscheint aber nicht im Bild.

## Korrektur in ts2.exe

- `--bin` schreibt `game/xbox_alpha.bin` (aus dem eigenen Image, nie ins
  Repo): je Inhalt (Hash der geladenen PC-Datei, HD zuerst) Breite, Höhe und
  RGBA in Upload-Reihenfolge. Alpha von der Xbox; Farbe von der PC-Datei,
  bei Leuchttexturen (vormultipliziert) von der Xbox. 18 Einträge (gleiche
  Inhalte nur einmal; deckt alle 27 Leuchttexturen der Liste).
- `src/host/alphafix.cpp` lädt die Datei beim Start; bei Zeichnungen mit
  SRC_ALPHA/INV_SRC_ALPHA bekommt eine Textur mit passendem Hash und Größe
  eine Kopie mit dem Xbox-Inhalt (Stufen aus 2×2-Mitteln). Ohne Datei (oder
  für andere Texturen) bleibt die Schätzung: Alpha aus der Helligkeit für
  kleine deckende Texturen mit schwarzem Hintergrund.
- Geprüft: Siberia-Laser (2452) bekommt den Xbox-Inhalt, Bild wie vorher.
