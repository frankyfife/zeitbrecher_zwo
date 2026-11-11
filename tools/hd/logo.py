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


def installieren(spiel, png, textur, schein, groesse=None, menueschein=False):
    hd = make_hd()
    im = Image.open(png)
    im.load()
    if im.mode != 'RGBA':
        print(f'Hinweis: {png} ist {im.mode}, ohne Alphakanal; Alpha wird 255 (deckend)')
    im = im.convert('RGBA')
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
        installieren(spiel, args.installieren, args.textur, args.schein, groesse, args.menueschein)
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
