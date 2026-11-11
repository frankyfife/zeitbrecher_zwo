"""Schriftzug "TimeSplitters 2" im Hauptmenü: exportieren und ersetzen.

Das Hauptmenü (0x1410B5A8C) zeichnet zwei Texturen übereinander, beide
512x256 RGBA, als ein Viereck x 40..1880, y -48..492 eines 1920x1080-Bildes
(Anzeige 1840x540, also 3,41:1; die Textur ist 2:1 und wird waagerecht
gestreckt):

* misc/ts2newlogo  der Schriftzug (Form im Alpha, gewöhnliche Mischung)
* misc/ts2glow     weißer Schein um die Buchstaben (RGB = Alpha, Buchstaben
                   ausgespart), additiv und pulsierend; dieselbe Textur
                   formt im ersten Bildschirm das brennende Logo
                   (src/host/logofix.cpp).

TS2 liest die Texturgröße aus der geladenen Datei und gibt die
Texturkoordinaten in ganzer Breite/Höhe an: Eine größere Datei unter
game/hd/ erscheint ohne Umrechnung an derselben Stelle.

TS2s .rgb-Dateien stehen für Pillow auf dem Kopf (Zeile 0 der Datei ist
oben im Spiel). Dieses Werkzeug dreht beim Export und beim Einspielen, die
PNGs stehen also aufrecht.

Aufrufe (aus dem Repo-Ordner):

  python -I tools/hd/logo.py --export [--ziel <ordner>]
      PNGs (aufrecht, mit Alpha) nach game/hd_eigen/logo
  python -I tools/hd/logo.py --installieren <bild.png> [--textur ts2newlogo] [--schein] [--groesse BxH]
      PNG -> game/hd_eigen/ts2/textures/misc/<textur>.rgb -> game/hd (alte Datei
      nach game/hd_backup); Größe 2:1 als Zweierpotenz bis 2048x1024, mit
      --groesse beliebig (1840x540 = 1:1 bei 1080 Zeilen, geprüft);
      --schein erzeugt ts2glow passend zum neuen Schriftzug (ändert auch das
      brennende Logo im ersten Bildschirm); --menueschein erzeugt stattdessen
      einen eigenen Schein nur für das Hauptmenü (ts2glow_menue.rgba,
      src/host/menulogo.cpp), das brennende Logo bleibt wie es ist
  python -I tools/hd/logo.py --zuruecksetzen [--textur ...] [--schein] [--menueschein]
  python -I tools/hd/logo.py --probe <bild.png>
      Prüfbild: Original 4fach (2048x1024) mit rotem Rahmen

Spieldaten und alles daraus Erzeugte gehören nie ins Repo.
"""
import argparse
import importlib.util
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MISC = os.path.join('ts2', 'textures', 'misc')
TEXTUREN = ('ts2newlogo', 'ts2glow')
BASIS = (512, 256)            # Originalgröße, Seitenverhältnis 2:1
GROESSEN = [(512 << k, 256 << k) for k in range(3)]  # 512x256, 1024x512, 2048x1024
ANZEIGE = (1840, 540)         # Fläche im 1920x1080-Bild
# Schein aus dem Alpha des Schriftzugs (an ts2logo/ts2glow gemessen, RMS 0,05):
# Gauß mit 8 Pixeln (bei 512 Breite), Verstärkung 1,75, Buchstaben ausgespart
SCHEIN_SIGMA, SCHEIN_GAIN = 8.0, 1.75


def make_hd():
    """sgi_speichern/installieren/zuruecksetzen aus make_hd.py (gleiche Regeln)."""
    pfad = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'make_hd.py')
    spec = importlib.util.spec_from_file_location('make_hd', pfad)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def lesen(pfad):
    """.rgb -> aufrechtes RGBA-Bild"""
    im = Image.open(pfad)
    im.load()
    return im.convert('RGBA').transpose(Image.FLIP_TOP_BOTTOM)


def export(spiel, ziel):
    os.makedirs(ziel, exist_ok=True)
    for name in TEXTUREN + ('ts2logo',):
        for art, wurzel in (('', 'ts2'), ('_hd', os.path.join('hd', 'ts2'))):
            q = os.path.join(spiel, wurzel, 'textures', 'misc', name + '.rgb')
            if not os.path.exists(q):
                continue
            im = lesen(q)
            p = os.path.join(ziel, f'{name}{art}_{im.width}x{im.height}.png')
            im.save(p)
            a = np.asarray(im)[..., 3]
            print(f'  {p}  ({Image.open(q).mode}, Alpha {a.min()}..{a.max()}, '
                  f'{(a > 0).mean() * 100:.0f} % sichtbar)')
    # Vorschau wie im Spiel: Schriftzug auf Grau, auf die Anzeigefläche gestreckt
    logo = lesen(os.path.join(spiel, MISC, 'ts2newlogo.rgb')).resize(ANZEIGE, Image.BILINEAR)
    bg = Image.new('RGBA', ANZEIGE, (60, 70, 100, 255))
    bg.alpha_composite(logo)
    p = os.path.join(ziel, 'vorschau_anzeige_1840x540.png')
    bg.convert('RGB').save(p)
    print(f'  {p}')


def zielgroesse(w, h):
    """kleinste Stufe (2:1, Zweierpotenz), die das Bild nicht verkleinert,
    höchstens 2048x1024"""
    for g in GROESSEN:
        if g[0] >= w and g[1] >= h:
            return g
    return GROESSEN[-1]


def schein_aus(logo):
    """ts2glow-Ersatz (RGBA, aufrecht, gleiche Größe) aus dem Alpha eines
    Schriftzugs; der Schein wird im Verhältnis 2:1 der Originaltextur
    gerechnet, damit er auch bei anderen Seitenverhältnissen rund bleibt."""
    w, h = logo.size
    alpha = logo.getchannel('A')
    a = np.asarray(alpha, np.float32) / 255.0
    rund = alpha.resize((w, w // 2), Image.BILINEAR)
    sigma = SCHEIN_SIGMA * w / BASIS[0]
    b = np.asarray(rund.filter(ImageFilter.GaussianBlur(sigma)).resize((w, h), Image.BILINEAR), np.float32) / 255.0
    g = np.uint8(np.clip(np.clip(b * SCHEIN_GAIN, 0, 1) * (1 - a) * 255 + 0.5, 0, 255))
    return Image.merge('RGBA', [Image.fromarray(g)] * 4)


MENUESCHEIN = 'ts2glow_menue.rgba'


def rgba_speichern(bild, pfad):
    """eigenes Format für src/host/menulogo.cpp: "TS2R", Breite, Höhe (u32),
    RGBA8 aufrecht (oberste Zeile zuerst, so wie sie im Spiel erscheint)"""
    import struct
    b = bild.convert('RGBA')
    with open(pfad, 'wb') as f:
        f.write(b'TS2R' + struct.pack('<II', b.width, b.height))
        f.write(b.tobytes())


# Lage des Original-Schriftzugs im 1920x1080-Bild (Alpha-Umriss von
# ts2newlogo (20,47)-(482,193) auf das Viereck x 40..1880, y -48..492)
ORIGINAL_LAGE = (112, 51, 1772, 359)
ANZEIGE_X0, ANZEIGE_Y0 = 40, -48   # obere linke Ecke der Anzeigefläche
LEINWAND = 2                       # Arbeitsfläche in doppelter Anzeigegröße


def anzeige_setzen(im, lage):
    """Schriftzug (Umriss seines Alphas) auf das Bildschirm-Viereck lage
    (x0, y0, x1, y1 im 1920x1080-Bild) strecken; Ergebnis: Arbeitsfläche
    in Anzeigeform (3680x1080), wird beim Einspielen auf 2:1 gebracht."""
    bb = im.getchannel('A').point(lambda v: 255 if v > 8 else 0).getbbox()
    x0, y0, x1, y1 = lage
    k = LEINWAND
    w, h = round((x1 - x0) * k), round((y1 - y0) * k)
    teil = im.crop(bb).resize((w, h), Image.LANCZOS)
    fl = Image.new('RGBA', (ANZEIGE[0] * k, ANZEIGE[1] * k), (0, 0, 0, 0))
    fl.alpha_composite(teil, (round((x0 - ANZEIGE_X0) * k), round((y0 - ANZEIGE_Y0) * k)))
    s = (x1 - x0) / (y1 - y0) / ((bb[2] - bb[0]) / (bb[3] - bb[1]))
    print(f'Schriftzug auf {x0},{y0}-{x1},{y1} gesetzt (waagerecht {s:.2f}fach gestreckt; '
          f'Original im Spiel 1,70fach)')
    return fl


def relief(im, staerke):
    """leichtes Relief wie beim Original: Kanten oben links heller, unten
    rechts dunkler (Licht von oben links auf die weich gerundete Form),
    dazu ein sanfter Verlauf von oben nach unten"""
    a = im.getchannel('A')
    pro_pixel = im.height / ANZEIGE[1]             # Bildpunkte je Anzeige-Pixel
    hoehe = np.asarray(a.filter(ImageFilter.GaussianBlur(4.0 * pro_pixel)), np.float32) / 255.0
    gy, gx = np.gradient(hoehe)
    licht = -(gx + gy) / np.sqrt(2.0)              # Licht von oben links
    licht = np.clip(licht / (np.percentile(np.abs(licht[np.asarray(a) > 0]), 98) + 1e-6), -1, 1)
    # Rand: Innenkante etwas dunkler (gefaste Kante wie beim Original)
    innen = np.asarray(a.filter(ImageFilter.GaussianBlur(1.5 * pro_pixel)), np.float32) / 255.0
    rand = np.clip(1.0 - innen, 0, 1)
    bb = a.getbbox() or (0, 0, im.width, im.height)
    zeilen = np.clip((np.arange(im.height, dtype=np.float32)[:, None] - bb[1]) / max(1, bb[3] - bb[1]), 0, 1)
    # Metallverlauf: oben hell, unten leicht graublau
    verlauf = 1.0 - staerke * zeilen[..., None] * np.array([0.42, 0.36, 0.22], np.float32)
    faktor = ((1.0 + staerke * licht) * (1.0 - 0.6 * staerke * rand))[..., None] * verlauf
    px = np.asarray(im, np.float32)
    rgb = np.clip(px[..., :3] * faktor, 0, 255)
    return Image.fromarray(np.dstack([rgb, px[..., 3]]).astype(np.uint8), 'RGBA')


def installieren(spiel, png, textur, schein, groesse=None, menueschein=False, lage=None, relief_staerke=0.0):
    hd = make_hd()
    im = Image.open(png)
    im.load()
    if im.mode != 'RGBA':
        print(f'Hinweis: {png} ist {im.mode}, ohne Alphakanal; Alpha wird 255 (deckend)')
    im = im.convert('RGBA')
    if lage:
        im = anzeige_setzen(im, lage)
    if relief_staerke > 0:
        im = relief(im, relief_staerke)
    w, h = im.size
    ziel = groesse or zielgroesse(w, h)
    verh = w / h
    if groesse:
        print(f'Größe {ziel[0]}x{ziel[1]} wie angegeben (auch ohne Zweierpotenz geprüft: 1840x540)')
    elif abs(verh - 2.0) > 0.02:
        art = 'Anzeige-Seitenverhältnis 3,41:1' if abs(verh - ANZEIGE[0] / ANZEIGE[1]) < 0.1 else f'{verh:.2f}:1'
        print(f'Hinweis: {w}x{h} ist {art}; wird auf {ziel[0]}x{ziel[1]} (2:1) gebracht. '
              f'Im Spiel erscheint es wieder auf {ANZEIGE[0]}x{ANZEIGE[1]} gestreckt.')
    if textur == 'ts2newlogo':
        # Farbe in durchsichtige Pixel ausbreiten: keine dunklen Säume beim Filtern
        px = np.asarray(im)
        rgb = hd._farbe_ausbluten(px[..., :3].astype(np.float64), px[..., 3] / 255.0, schritte=16)
        im = Image.fromarray(np.dstack([np.uint8(np.clip(rgb + 0.5, 0, 255)), px[..., 3]]), 'RGBA')
    if (w, h) != ziel:
        im = im.resize(ziel, Image.LANCZOS)
    bilder = [(textur, im)]
    if schein:
        bilder.append(('ts2glow', schein_aus(im)))
    for name, b in bilder:
        p = os.path.join(spiel, 'hd_eigen', MISC, name + '.rgb')
        os.makedirs(os.path.dirname(p), exist_ok=True)
        # liegt in game/hd noch unsere frühere Fassung, sie nicht als
        # "Original" nach hd_backup sichern lassen (sonst holt
        # --zuruecksetzen die alte eigene Fassung statt des Originals)
        z = os.path.join(spiel, 'hd', MISC, name + '.rgb')
        if os.path.exists(p) and os.path.exists(z):
            import filecmp
            if filecmp.cmp(p, z, shallow=False):
                os.remove(z)
        hd.sgi_speichern(b.transpose(Image.FLIP_TOP_BOTTOM), p)
        print(f'  {p}  {b.width}x{b.height} RGBA')
    hd.installieren(spiel, ['misc/' + n for n, _ in bilder])
    if menueschein:
        g = schein_aus(im)
        for wurzel in ('hd_eigen', 'hd'):
            p = os.path.join(spiel, wurzel, MISC, MENUESCHEIN)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            rgba_speichern(g, p)
            print(f'  {p}  {g.width}x{g.height} (Schein nur im Hauptmenü)')


def probe(spiel, ausgabe):
    """Original 4fach mit rotem Rahmen und Schrift: zeigt im Spiel Lage, Größe, Schärfe"""
    im = lesen(os.path.join(spiel, MISC, 'ts2newlogo.rgb')).resize(GROESSEN[2], Image.LANCZOS)
    d = ImageDraw.Draw(im)
    w, h = im.size
    d.rectangle((0, 0, w - 1, h - 1), outline=(255, 0, 0, 255), width=12)
    d.line((0, 0, w - 1, h - 1), fill=(255, 0, 0, 160), width=4)
    d.text((40, 40), f'PROBE {w}x{h}', fill=(255, 255, 0, 255))
    im.save(ausgabe)
    print(f'  {ausgabe}  {w}x{h}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--spiel', default=os.path.join(REPO, 'game'), help='game-Ordner (Standard: <Repo>/game)')
    ap.add_argument('--export', action='store_true', help='Texturen als PNG exportieren')
    ap.add_argument('--ziel', help='Ordner für --export (Standard: <spiel>/hd_eigen/logo)')
    ap.add_argument('--installieren', metavar='PNG', help='PNG als Ersatztextur einspielen')
    ap.add_argument('--textur', default='ts2newlogo', choices=TEXTUREN, help='welche Textur (Standard ts2newlogo)')
    ap.add_argument('--schein', action='store_true', help='ts2glow aus dem neuen Schriftzug erzeugen und mit einspielen')
    ap.add_argument('--menueschein', action='store_true',
                    help='eigenen Schein nur für das Hauptmenü erzeugen (brennendes Logo bleibt)')
    ap.add_argument('--lage', metavar='X0,Y0,X1,Y1',
                    help='Schriftzug auf dieses Viereck im 1920x1080-Bild strecken; "original" = '
                         'Lage des Original-Schriftzugs (112,51,1772,359)')
    ap.add_argument('--relief', type=float, default=0.0, metavar='STAERKE',
                    help='leichtes Relief wie das Original (z. B. 0.35; 0 = aus)')
    ap.add_argument('--groesse', metavar='BxH', help='Texturgröße erzwingen, z. B. 1840x540 (Standard: 2:1, Zweierpotenz)')
    ap.add_argument('--zuruecksetzen', action='store_true', help='game/hd aus game/hd_backup wiederherstellen')
    ap.add_argument('--probe', metavar='PNG', help='Prüfbild (4fach, Rahmen) schreiben')
    args = ap.parse_args()
    spiel = os.path.abspath(args.spiel)
    if not os.path.exists(os.path.join(spiel, MISC, 'ts2newlogo.rgb')):
        sys.exit(f'keine Originaltexturen unter {os.path.join(spiel, MISC)}')
    if args.export:
        export(spiel, args.ziel or os.path.join(spiel, 'hd_eigen', 'logo'))
    elif args.installieren:
        groesse = tuple(int(x) for x in args.groesse.lower().split('x')) if args.groesse else None
        lage = None
        if args.lage:
            lage = ORIGINAL_LAGE if args.lage == 'original' else tuple(float(v) for v in args.lage.split(','))
        installieren(spiel, args.installieren, args.textur, args.schein, groesse, args.menueschein,
                     lage, args.relief)
    elif args.zuruecksetzen:
        make_hd().zuruecksetzen(spiel, ['misc/' + args.textur] + (['misc/ts2glow'] if args.schein else []))
        if args.menueschein:
            p = os.path.join(spiel, 'hd', MISC, MENUESCHEIN)
            if os.path.exists(p):
                os.remove(p)
                print(f'  entfernt: {p}')
    elif args.probe:
        probe(spiel, args.probe)
    else:
        ap.print_help()


if __name__ == '__main__':
    main()
