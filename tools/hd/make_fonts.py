"""Scharfe Schriften für Zeitbrecher Zwo (TS2-PC-Port).

TS2 zeichnet Text mit Schrift-Texturen (game/ts2/textures/misc/impact.rgb,
book.rgb, booklight.rgb): weiße Zeichen, Form im Alphakanal, in
Konsolen-Auflösung. Bei 1920x1080 werden sie 2- bis 4fach vergrößert und
unscharf. Dieses Werkzeug macht daraus Umrisse (SVG) und zeichnet sie in
4facher Größe kantenglatt neu, mit derselben Anordnung der Zeichen:

1. Nachzeichnen, je Zeichen (Zellen aus den Zeichentabellen in
   game/ts2.bin): Aus dem Alphakanal wird in 8facher Abtastung eine
   glatte, scharf begrenzte Form geschätzt, deren Kantenglättung auf
   Originalgröße wieder das Original ergibt (Rückprojektion mit
   Weichzeichnen, `feld`). Spiegelsymmetrische Zeichen (A, H, O, V ...)
   werden an ihrer besten Achse gemittelt. Die Kante wird verfolgt
   (Marching Squares) und dann geordnet (`ordnen`):
   - optimale Zerlegung in Geraden und Kreisbögen (dynamische
     Programmierung, `segmentieren`),
   - Vielecke aus kurzen Geraden an Rundungen werden wieder Kurven,
     abgerundete Ecken zwischen zwei Geraden spitz, runde Strichenden
     gerade Kappen (waagerecht/senkrecht), fast gleiche Geraden eine,
   - fast achsparallele Geraden werden exakt waagerecht/senkrecht,
   - schriftweit: waagerechte Kanten auf gemeinsame Höhen (Grundlinie,
     Versal-, x-Höhe; nur starke Häufungen), gleich dicke Striche von
     Buchstaben und Ziffern auf eine gemeinsame Stärke,
   - Ecken = Schnittpunkte der Geraden, Kurven geglättet und angeglichen.
   Weicht ein Zeichen so deutlich stärker vom Original ab als mit der
   einfachen Glättung (früheres Verfahren), bleibt es bei dieser
   (Sicherheitsnetz, z. B. für sehr dünne Zeichen). Ergebnis:
   game/hd_eigen/schrift/<name>.svg, ein Blatt je Schrift in
   Original-Pixeln, Zeichen aufrecht, schwarz auf durchsichtig. Das SVG
   ist die Vorlage zum Nachbessern (z. B. in Inkscape: Pfade bearbeiten;
   die Ebene "vorlage" zeigt das Original).
2. Zeichnen: Die Pfade aus dem SVG werden in der Zielgröße mit genauer
   Flächendeckung gefüllt (Gerade/Bögen/Kurven, Füllregeln nonzero und
   evenodd), auf die Zeichenzellen beschnitten (kein Überlauf in die
   Lücken, die das Spiel beim Filtern mitliest) und als SGI-Textur (RGB
   weiß, Alpha = Deckung) nach game/hd_eigen/ts2/textures/misc/<name>.rgb
   geschrieben.

Die SVGs sind aus den Spieldaten abgeleitet und gehören wie alles unter
game/ nie ins Repository. Ins Repo gehört nur dieses Skript.

ts2.exe braucht dazu die Umrechnung der Zeichenkoordinaten
(src/host/fontfix.cpp): ohne sie zeigt eine größere Schrift-Textur nur ihr
oberes linkes Viertel. Siehe docs/hd-texturen.md "Schriften".

Aufrufe (aus dem Repo-Ordner, Python mit Pillow und numpy):

  python -I tools/hd/make_fonts.py                 SVG anlegen (falls fehlt), Texturen zeichnen
  python -I tools/hd/make_fonts.py --svg           SVG neu aus dem Original (überschreibt Änderungen!)
  python -I tools/hd/make_fonts.py --svg --einfach SVG nur geglättet (früheres Verfahren, ohne Ordnen)
  python -I tools/hd/make_fonts.py --nur book      nur diese Schrift(en)
  python -I tools/hd/make_fonts.py --faktor 2      andere Vergrößerung (Standard 4)
  python -I tools/hd/make_fonts.py --messen        Abweichung (verkleinert gegen Original)
  python -I tools/hd/make_fonts.py --vergleich <Ordner>   PNG: Original vergrößert | neu
  python -I tools/hd/make_fonts.py --installieren  hd_eigen -> game/hd (Sicherung hd_backup)
  python -I tools/hd/make_fonts.py --zuruecksetzen game/hd aus hd_backup / eigene entfernen
"""
import argparse
import base64
import importlib.util
import io
import math
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image, ImageDraw

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MISC = os.path.join('ts2', 'textures', 'misc')

# Schriften der Tabelle 0x14259BF60 mit Zeichen (fontcaps hat keine
# Zeichentabelle und wird nicht benutzt). schwelle: Deckung, an der die
# Kante liegt (0,5 = Mitte der Kantenglättung; kleiner = kräftiger).
SCHRIFTEN = {
    'impact': dict(unschaerfe=0.15, haerte=10.0, kappe_breite=6.0),
    'book': dict(unschaerfe=0.2, haerte=8.0),
    'booklight': dict(unschaerfe=0.2, haerte=8.0),
}
# Gemeinsame Werte (am Bild verglichen, siehe docs/hd-texturen.md):
# schwelle    Deckung, an der die Kante liegt
# iterationen Durchgänge der Formschätzung (feld)
# unschaerfe  Weichzeichnen je Durchgang in Original-Pixeln (glatter, aber runder)
# haerte      Zuspitzung je Durchgang (größer = härtere Kante, mehr Treppen)
# sigma       Glättung der Linienzüge in Original-Pixeln (Ecken bleiben fest)
# schaerfen   Ecken mit geraden Nachbarstücken spitz wiederherstellen
GEMEINSAM = dict(schwelle=0.5, iterationen=30, sigma=0.15, schaerfen=True)
# Ordnen der Umrisse (Längen in Original-Pixeln, Winkel in Grad; Stand: am Bild
# und mit --messen verglichen, siehe docs/hd-texturen.md "Schriften")
ORDNEN = dict(
    schritt=0.1,            # Abtastabstand der Linienzüge
    sigma_vor=0.08,         # leichte Glättung vor dem Zerlegen
    seg_max=20.0,           # längstes Stück der Zerlegung
    r_min=0.9, r_max=60.0,  # erlaubte Bogenradien (kleiner: Ecke, größer: Gerade)
    lam_gerade=0.03,        # Preis je Gerade bzw. Bogen (Fehlerquadrate in px²): weniger,
    lam_bogen=0.08,         # aber genaue Stücke; Geraden etwas billiger als Bögen
    knick_gerade=35.0, knick_stoss=25.0, knick_bogen=40.0,  # Ecke ab diesem Knick (G-G, G-B, B-B)
    poly_max=2.5, poly_knick=(6.0, 45.0), poly_min=3,      # Vieleck an Rundungen -> Kurve
    kurz_gerade=1.0,        # kürzere Geraden gelten als Teil einer Ecke/Kappe
    kurve_lang=1.5, kurve_r=1.8,  # Bögen ab dieser Länge/diesem Radius zählen wie Geraden (Tangente)
    gleich_grad=8.0,        # zwei Geraden mit weniger Knick werden eine
    max_gerade=0.09,        # größter Abstand der Punkte einer zusammengefassten Geraden
    ecke_kurve_max=2.5, ecke_winkel=25.0, ecke_abstand=0.5,  # abgerundete Ecke -> spitz
    kappen=True, kappe_winkel=120.0, kappe_winkel_bogen=150.0, kappe_max=8.0,
    kappe_abstand=0.7, kappe_breite=2.6,  # rundes Strichende -> gerade Kappe
    min_gerade_frei=3.5,    # kürzere Geraden ohne Ecke an beiden Enden werden Kurve
    achse_grad=4.0, achse_versatz=0.15,   # achsparallel, wenn die Enden höchstens so weit wandern
    max_schnitt=0.8,        # Eckpunkt höchstens so weit vom nachgezeichneten Umriss
    sigma_kurve=0.2, angleichen=0.6,      # Glättung der Kurven, Übergang zu verschobenen Ecken
    metrik_radius=0.2, metrik_luecke=0.15, metrik_min=8,  # gemeinsame Höhen
    staerke_radius=0.2, staerke_luecke=0.1, staerke_min_n=3, staerke_max=4.0, staerke_min_laenge=1.0,
    max_sym_rest=0.15,      # Spiegelsymmetrie nur, wenn das Zeichen fast symmetrisch ist
    val_flaeche=0.2, val_abstand=0.9,     # geordneter Ring muss nahe am nachgezeichneten liegen
    netz_faktor=1.5, netz_abstand=4.0,    # Sicherheitsnetz: RMS neu > alt * f + a/255 -> alt
)
# Zeichen, die in allen drei Schriften spiegelsymmetrisch angelegt sind
SYMMETRISCH = set('AHIMOTUVWXYovwxil08!\'"*+-=^_.:')
FAKTOR = 4      # Vergrößerung der Ausgabe
ABTAST = 8      # Feinabtastung beim Nachzeichnen (je Original-Pixel)
RAND = 2        # Rand in Original-Pixeln beim Nachzeichnen
TOLERANZ = 0.02  # Vereinfachung der Linienzüge (Original-Pixel)
ZEILEN = 16     # Abtastzeilen je Ausgabe-Pixel beim Füllen (waagerecht genau)

SVG_NS = 'http://www.w3.org/2000/svg'
XLINK_NS = 'http://www.w3.org/1999/xlink'


# ======================================================================
# Original laden / Textur speichern
# ======================================================================

def original_alpha(spiel, name):
    """Alphakanal des Originals als float 0..1, Zeichen aufrecht.

    SGI speichert die Zeilen von unten nach oben, TS2 liest sie von oben:
    in Pillows Ansicht stehen die Zeichen auf dem Kopf. Für das SVG wird
    gespiegelt, beim Speichern zurück."""
    im = Image.open(os.path.join(spiel, MISC, name + '.rgb'))
    im.load()
    if im.mode != 'RGBA':
        raise SystemExit(f'{name}: erwartet RGBA, ist {im.mode}')
    a = np.asarray(im.getchannel('A'), dtype=np.float32) / 255.0
    return np.flipud(a).copy(), im.size


def sgi_speichern(im, pfad):
    """SGI unkomprimiert, Bildname im Kopf leer (sonst Absturz beim Laden,
    siehe docs/hd-texturen.md)."""
    os.makedirs(os.path.dirname(pfad), exist_ok=True)
    im.save(pfad, format='SGI')
    with open(pfad, 'r+b') as f:
        f.seek(24)
        f.write(bytes(80))


def textur_aus_deckung(deckung):
    """Deckung (aufrecht, 0..1) -> RGBA-Bild wie das Original (RGB weiß)."""
    a = np.flipud(np.clip(np.rint(deckung * 255.0), 0, 255).astype(np.uint8))
    h, w = a.shape
    rgb = np.full((h, w, 3), 255, np.uint8)
    return Image.fromarray(np.dstack([rgb, a]), 'RGBA')


# Schrifttabelle in ts2.bin (siehe src/host/fontfix.cpp): je Schrift 0x30 Byte,
# +0 Zeiger auf den Texturnamen, +0x10 Zeiger auf die Zeichentabelle (je Zeichen
# ab ' ' 10 Byte: u16 x0, y0, x1, y1 (x1/y1 ausschließlich), s8 Versatz nach unten)
SCHRIFT_TABELLE = 0x14259BF60
SCHRIFT_ANZAHL = 4
ZEICHEN_ANZAHL = 102  # ' ' bis 0x85


def _ts2bin_leser(spiel):
    """game/ts2.bin (Kopf, dann Bereiche rva/Größe/Art/Daten) -> Lesefunktion(va, n)."""
    with open(os.path.join(spiel, 'ts2.bin'), 'rb') as f:
        v = f.read()
    _, ver, _, _, basis = struct.unpack_from('<IIIIQ', v, 0)
    if ver != 1:
        raise ValueError(f'ts2.bin Version {ver}')
    o = 24
    (n,) = struct.unpack_from('<I', v, o)
    o += 4
    bereiche = []
    for _ in range(n):
        rva, groesse, _ = struct.unpack_from('<III', v, o)
        o += 12
        bereiche.append((rva, o, groesse))
        o += groesse

    def lesen(va, k):
        r = va - basis
        for rva, ab, groesse in bereiche:
            if rva <= r and r + k <= rva + groesse:
                return v[ab + r - rva:ab + r - rva + k]
        raise KeyError(hex(va))
    return lesen


def zeichentabelle(spiel, name):
    """Zellen der Schrift: Liste (Zeichencode, x0, y0, x1, y1, Versatz); None ohne ts2.bin."""
    try:
        lesen = _ts2bin_leser(spiel)
        for i in range(SCHRIFT_ANZAHL):
            eintrag = SCHRIFT_TABELLE + i * 0x30
            (zname,) = struct.unpack('<Q', lesen(eintrag, 8))
            (tab,) = struct.unpack('<Q', lesen(eintrag + 0x10, 8))
            if not zname or not tab:
                continue
            text = lesen(zname, 48).split(b'\0')[0].decode('ascii', 'replace')
            if os.path.splitext(text.rsplit('/', 1)[-1])[0] != name:
                continue
            zellen = []
            for k in range(1, ZEICHEN_ANZAHL):
                x0, y0, x1, y1, versatz = struct.unpack('<HHHHb', lesen(tab + 10 * k, 9))
                if x1 > x0 and y1 > y0:
                    zellen.append((32 + k, x0, y0, x1, y1, versatz))
            return zellen
    except (OSError, KeyError, ValueError, struct.error) as e:
        print(f'  {name}: keine Zeichentabelle aus ts2.bin ({e})')
    return None


# ======================================================================
# Nachzeichnen: Marching Squares auf dem fein abgetasteten Alphakanal
# ======================================================================

# Ecken einer Zelle (x, y; y nach unten): a oben links, b oben rechts,
# c unten rechts, d unten links. Kanten: T(a-b), R(b-c), B(d-c), L(a-d).
_ECKE = {'a': (0, 0), 'b': (1, 0), 'c': (1, 1), 'd': (0, 1)}
_KANTE = {'T': (0.5, 0), 'R': (1, 0.5), 'B': (0.5, 1), 'L': (0, 0.5)}
_ECK_KANTEN = {'a': 'TL', 'b': 'TR', 'c': 'RB', 'd': 'BL'}


def _ausrichten(p, q, ecke, innen):
    """Strecke p->q so drehen, dass innen liegende Ecken links (Kreuzprodukt > 0) liegen."""
    (px, py), (qx, qy), (ex, ey) = _KANTE[p], _KANTE[q], _ECKE[ecke]
    kreuz = (qx - px) * (ey - py) - (qy - py) * (ex - px)
    return (p, q) if (kreuz > 0) == innen else (q, p)


def _tabelle():
    """Fall (Bits a=8, b=4, c=2, d=1) -> Strecken; Sattel je nach Mitte."""
    tab = {}
    for fall in range(16):
        drin = {e: bool(fall & bit) for e, bit in zip('abcd', (8, 4, 2, 1))}
        geaendert = [k for k, (e1, e2) in zip('TRBL', ('ab', 'bc', 'dc', 'ad')) if drin[e1] != drin[e2]]
        if len(geaendert) == 2:
            ecke = next(e for e in 'abcd' if drin[e])
            tab[fall] = {False: [_ausrichten(*geaendert, ecke, True)]}
            tab[fall][True] = tab[fall][False]
        elif len(geaendert) == 4:
            tab[fall] = {}
            for mitte_drin in (False, True):
                # Mitte innen: die äußeren Ecken werden abgeschnitten, sonst die inneren
                ecken = [e for e in 'abcd' if drin[e] != mitte_drin]
                tab[fall][mitte_drin] = [_ausrichten(*_ECK_KANTEN[e], e, drin[e]) for e in ecken]
    return tab


_TAB = _tabelle()


def _rdp(p, eps):
    """Ramer-Douglas-Peucker für einen offenen Linienzug (n x 2)."""
    behalten = np.zeros(len(p), bool)
    behalten[0] = behalten[-1] = True
    stapel = [(0, len(p) - 1)]
    while stapel:
        i, j = stapel.pop()
        if j <= i + 1:
            continue
        a, b = p[i], p[j]
        d = b - a
        n = math.hypot(*d)
        q = p[i + 1:j] - a
        if n < 1e-12:
            dist = np.hypot(q[:, 0], q[:, 1])
        else:
            dist = np.abs(q[:, 0] * d[1] - q[:, 1] * d[0]) / n
        k = int(np.argmax(dist))
        if dist[k] > eps:
            m = i + 1 + k
            behalten[m] = True
            stapel += [(i, m), (m, j)]
    return p[behalten]


def _vereinfachen(ring, eps):
    """Geschlossenen Ring vereinfachen (an zwei weit entfernten Punkten teilen)."""
    if len(ring) < 8:
        return ring
    j = int(np.argmax(np.hypot(*(ring - ring[0]).T)))
    a = _rdp(ring[:j + 1], eps)
    b = _rdp(np.vstack([ring[j:], ring[:1]]), eps)
    return np.vstack([a[:-1], b[:-1]])


def _gleichmaessig(ring, schritt):
    """Geschlossenen Ring in gleichen Bogenlängen neu abtasten."""
    p = np.vstack([ring, ring[:1]])
    seg = np.hypot(*np.diff(p, axis=0).T)
    s = np.concatenate([[0], np.cumsum(seg)])
    n = max(8, int(round(s[-1] / schritt)))
    t = np.arange(n) * (s[-1] / n)
    return np.stack([np.interp(t, s, p[:, 0]), np.interp(t, s, p[:, 1])], 1), s[-1] / n


def _glaetten(p, sigma, geschlossen):
    """Gauß-Glättung der Punkte (sigma in Punkten); offene Stücke mit festen,
    punktgespiegelten Enden (Geraden bleiben gerade bis in die Ecke)."""
    if sigma <= 0 or len(p) < 3:
        return p
    r = min(int(math.ceil(3 * sigma)), len(p) - 1)
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    if geschlossen:
        q = np.vstack([p[-r:], p, p[:r]])
    else:
        vorn = 2 * p[0] - p[1:r + 1][::-1]
        hinten = 2 * p[-1] - p[-r - 1:-1][::-1]
        q = np.vstack([vorn, p, hinten])
    x = np.convolve(q[:, 0], k, mode='valid')
    y = np.convolve(q[:, 1], k, mode='valid')
    out = np.stack([x, y], 1)
    if not geschlossen:
        out[0], out[-1] = p[0], p[-1]
    return out


def _ecken(p, schritt, fenster, knick):
    """Indizes der Ecken: Richtungsänderung über ±fenster größer als knick (Grad)."""
    n = len(p)
    m = max(2, int(round(fenster / schritt)))
    if n < 4 * m:
        return []
    v1 = p - np.roll(p, m, axis=0)
    v2 = np.roll(p, -m, axis=0) - p
    w = np.degrees(np.abs(np.arctan2(v1[:, 0] * v2[:, 1] - v1[:, 1] * v2[:, 0], (v1 * v2).sum(1))))
    ecken = []
    for i in np.argsort(-w):
        if w[i] < knick:
            break
        if all(min(abs(i - j), n - abs(i - j)) > m for j in ecken):
            ecken.append(int(i))
    return sorted(ecken)


def _gerade(pts):
    """Ausgleichsgerade: (Punkt, Richtung, größter Abstand)."""
    c = pts.mean(0)
    _, _, vt = np.linalg.svd(pts - c)
    d = vt[0]
    abst = np.abs((pts - c) @ np.array([-d[1], d[0]]))
    return c, d, float(abst.max())


def formen_glaetten(ring, schritt=0.05, fenster=0.8, knick=50.0, sigma=0.15, schaerfen=False, spitze=0.7,
                    gerade=(0.6, 2.0)):
    """Rasterwellen der Nachzeichnung entfernen, Ecken wiederherstellen.

    Die Kante bei halber Deckung schwankt mit dem Pixelraster und rundet
    Ecken ab. Hier: gleichmäßig abtasten, Ecken suchen (Knick über
    ±fenster Pixel), die Stücke dazwischen mit Gauß (sigma Pixel) glätten
    und jede Ecke, deren Nachbarstücke gerade sind, durch den Schnittpunkt
    der beiden Ausgleichsgeraden ersetzen (die Punkte im Umkreis von
    `spitze` Pixeln entfallen)."""
    p, ds = _gleichmaessig(ring, schritt)
    n = len(p)
    ecken = _ecken(p, ds, fenster, knick)
    sig = sigma / ds
    if not ecken:
        return _glaetten(p, sig, True)
    # Stücke zwischen den Ecken glätten (Ecken bleiben fest)
    neu = p.copy()
    for a, b in zip(ecken, ecken[1:] + [ecken[0] + n]):
        idx = np.arange(a, b + 1) % n
        neu[idx] = _glaetten(p[idx], sig, False)
    p = neu
    if not schaerfen:
        return p
    # Ecken schärfen
    ersatz = {}
    weg = np.zeros(n, bool)
    for j, e in enumerate(ecken):
        vor = ecken[j - 1] if j > 0 else ecken[-1] - n
        nach = ecken[j + 1] if j + 1 < len(ecken) else ecken[0] + n
        g0, g1 = int(gerade[0] / ds), int(gerade[1] / ds)
        lim_v, lim_n = (e - vor) // 2, (nach - e) // 2
        if min(lim_v, lim_n) <= g0 + 2:
            continue
        iv = np.arange(e - min(g1, lim_v), e - g0) % n
        inn = np.arange(e + g0, e + min(g1, lim_n)) % n
        c1, d1, r1 = _gerade(p[iv])
        c2, d2, r2 = _gerade(p[inn])
        if max(r1, r2) > 0.08:
            continue
        det = d1[0] * (-d2[1]) - d1[1] * (-d2[0])
        if abs(det) < 0.3:  # fast parallel
            continue
        rhs = c2 - c1
        t = (rhs[0] * (-d2[1]) - rhs[1] * (-d2[0])) / det
        x = c1 + t * d1
        if math.hypot(*(x - p[e])) > 1.0:
            continue
        k = int(spitze / ds)
        k = min(k, lim_v - 1, lim_n - 1)
        weg[np.arange(e - k, e + k + 1) % n] = True
        ersatz[e] = x
    if not ersatz:
        return p
    out = []
    for i in range(n):
        if i in ersatz:
            out.append(ersatz[i])
        elif not weg[i]:
            out.append(p[i])
    return np.array(out)


def flaeche(ring):
    x, y = ring[:, 0], ring[:, 1]
    return 0.5 * float(np.dot(x, np.roll(y, -1)) - np.dot(np.roll(x, -1), y))


def _gauss(f, s):
    """Trennbare Gauß-Unschärfe (s in Feld-Pixeln), Rand fortgesetzt."""
    if s <= 0:
        return f
    r = int(math.ceil(3 * s))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / s) ** 2)
    k /= k.sum()
    h, w = f.shape
    g = np.pad(f, ((r, r), (0, 0)), mode='edge')
    f = sum(k[i] * g[i:i + h] for i in range(2 * r + 1))
    g = np.pad(f, ((0, 0), (r, r)), mode='edge')
    return sum(k[i] * g[:, i:i + w] for i in range(2 * r + 1))


def feld(alpha, abtast, iterationen, unschaerfe, haerte):
    """Fein abgetastetes Formfeld (0..1) zum Alphakanal.

    Ausgang ist die bikubische Vergrößerung. Dann abwechselnd: (1) das
    Feld so verschieben, dass seine Deckung je Original-Pixel (Mittel über
    abtast x abtast) wieder dem Original entspricht, (2) leicht weichzeichnen
    (glättet Kanten, `unschaerfe` in Original-Pixeln), (3) zu einer fast
    harten Form zuspitzen (Sigmoid mit `haerte`). Ergebnis: eine glatte,
    scharf begrenzte Form, deren Kantenglättung auf Originalgröße das
    Original ergibt (Strichstärke und Ecken bleiben erhalten)."""
    h, w = alpha.shape
    F = np.asarray(Image.fromarray(alpha.astype(np.float32), 'F').resize((w * abtast, h * abtast), Image.BICUBIC),
                   dtype=np.float64)
    F = np.clip(F, 0.0, 1.0)
    for _ in range(iterationen):
        rest = alpha - F.reshape(h, abtast, w, abtast).mean(axis=(1, 3))
        F = F + np.repeat(np.repeat(rest, abtast, axis=0), abtast, axis=1)
        F = _gauss(F, unschaerfe * abtast)
        F = 1.0 / (1.0 + np.exp(-(F - 0.5) * haerte))
    return F


def nachzeichnen(alpha, schwelle=0.5, abtast=ABTAST, rand=RAND, toleranz=TOLERANZ, iterationen=30,
                 unschaerfe=0.25, haerte=6.0, **glatt):
    """Einfaches Verfahren: Kanten der Form als geschlossene, geglättete Ringe
    (Original-Pixel, y nach unten)."""
    F = feld(np.pad(alpha, rand), abtast, iterationen, unschaerfe, haerte)
    ringe = []
    for pts in kanten(F, schwelle, abtast, rand):
        if len(pts) >= 8:
            pts = formen_glaetten(pts, **glatt)
        pts = _vereinfachen(pts, toleranz)
        if len(pts) >= 3 and abs(flaeche(pts)) > 0.02:
            ringe.append(pts)
    return ringe


def kanten(F, schwelle, abtast, rand):
    """Marching Squares auf dem Feld: rohe Ringe in Pixeln des Ausschnitts
    (ohne Rand). Die Tinte liegt links der Laufrichtung (Normale (-dy, dx)),
    wenn der äußere Ring positive Fläche hat; das gilt für alle Ringe gleich."""
    h, w = F.shape
    drin = F > schwelle
    fall = (drin[:-1, :-1] * 8 + drin[:-1, 1:] * 4 + drin[1:, 1:] * 2 + drin[1:, :-1] * 1).astype(np.int8)
    zr, zc = np.nonzero((fall != 0) & (fall != 15))
    mitte = (F[:-1, :-1] + F[:-1, 1:] + F[1:, 1:] + F[1:, :-1]) / 4 > schwelle

    def punkt(kid):
        if kid < h * w:  # waagerecht zwischen (r, c) und (r, c+1)
            r, c = divmod(kid, w)
            v0, v1 = F[r, c], F[r, c + 1]
            return c + (schwelle - v0) / (v1 - v0), r
        r, c = divmod(kid - h * w, w)  # senkrecht zwischen (r, c) und (r+1, c)
        v0, v1 = F[r, c], F[r + 1, c]
        return c, r + (schwelle - v0) / (v1 - v0)

    weiter = {}
    for r, c in zip(zr.tolist(), zc.tolist()):
        kid = {'T': r * w + c, 'B': (r + 1) * w + c, 'L': h * w + r * w + c, 'R': h * w + r * w + c + 1}
        for p, q in _TAB[int(fall[r, c])][bool(mitte[r, c])]:
            weiter[kid[p]] = kid[q]
    ringe = []
    while weiter:
        start, nxt = weiter.popitem()
        kette = [start]
        while nxt != start:
            kette.append(nxt)
            nxt = weiter.pop(nxt)
        pts = np.array([punkt(k) for k in kette], np.float64)
        # Feinraster -> Original-Pixel (Pixelmitte bei +0,5)
        ringe.append((pts + 0.5) / abtast - rand)
    return ringe


def _im_ring(pt, ring):
    x, y = pt
    xs, ys = ring[:, 0], ring[:, 1]
    xs2, ys2 = np.roll(xs, -1), np.roll(ys, -1)
    kreuzt = (ys > y) != (ys2 > y)
    with np.errstate(divide='ignore', invalid='ignore'):
        xi = xs + (y - ys) * (xs2 - xs) / (ys2 - ys)
    return int(np.count_nonzero(kreuzt & (x < xi))) % 2 == 1


def gruppieren(ringe):
    """Äußere Ringe mit ihren Löchern zusammenfassen (je Gruppe ein Pfad)."""
    if not ringe:
        return []
    fl = [flaeche(r) for r in ringe]
    aussen_vz = math.copysign(1, fl[int(np.argmax(np.abs(fl)))])
    aussen = [i for i, f in enumerate(fl) if math.copysign(1, f) == aussen_vz]
    gruppen = {i: [ringe[i]] for i in aussen}
    for i, f in enumerate(fl):
        if i in gruppen:
            continue
        kandidaten = [j for j in aussen if _im_ring(ringe[i][0], ringe[j])]
        if kandidaten:
            j = min(kandidaten, key=lambda j: abs(fl[j]))
            gruppen[j].append(ringe[i])
        else:
            gruppen[i] = [ringe[i]]
    # nach Lage sortieren (Zeilen, dann Spalten), damit das SVG lesbar bleibt
    def lage(g):
        r = g[0]
        return (round(float(r[:, 1].min()) / 8), float(r[:, 0].min()))
    return sorted(gruppen.values(), key=lage)


# ======================================================================
# Ordnen: Geraden und Bögen, spitze Ecken, Kappen, gemeinsame Maße
# ======================================================================

class Gerade:
    """Gerade durch c mit Richtung d (in Laufrichtung); i0..i1 Indizes im Ring."""

    def __init__(self, i0, i1, c, d):
        self.i0, self.i1 = i0, i1
        self.c = np.asarray(c, float)
        self.d = np.asarray(d, float) / math.hypot(*d)
        self.achse = None   # 'h' oder 'v', wenn achsparallel
        self.fest = False   # Kappe oder zusammengefasst (zählt immer als lang)

    def normale(self):
        return np.array([-self.d[1], self.d[0]])

    def proj(self, pt):
        return self.c + np.dot(np.asarray(pt) - self.c, self.d) * self.d


class Kurve:
    def __init__(self, i0, i1, kreis=None):
        self.i0, self.i1 = i0, i1
        self.kreis = kreis  # (Mitte, Radius) eines Bogens aus der Zerlegung


def _laenge(e, ds):
    return (e.i1 - e.i0) * ds


def _stueck(p, i0, i1):
    n = len(p)
    while i1 < i0:
        i1 += n
    return p[np.arange(i0, i1 + 1) % n]


def _ausgleich(pts):
    """Ausgleichsgerade: Schwerpunkt, Richtung, Abstände der Punkte."""
    c = pts.mean(0)
    _, _, vt = np.linalg.svd(pts - c)
    d = vt[0]
    return c, d, np.abs((pts - c) @ np.array([-d[1], d[0]]))


def _schnitt(g1, g2):
    det = g1.d[0] * (-g2.d[1]) - g1.d[1] * (-g2.d[0])
    if abs(det) < 0.25:  # fast parallel
        return None
    r = g2.c - g1.c
    t = (r[0] * (-g2.d[1]) - r[1] * (-g2.d[0])) / det
    return g1.c + t * g1.d


def _kosten(q, kmax, rmin, rmax):
    """Fehlerquadratsummen aller Stücke q[i..i+l] (l = 2..kmax) als Gerade
    (kleinster Eigenwert) und als Kreisbogen (Kasa-Anpassung, auf Abstände
    umgerechnet); Bögen außerhalb rmin..rmax sind nicht erlaubt."""
    n = len(q)
    x, y = q[:, 0], q[:, 1]
    z = x * x + y * y
    S = {k: np.concatenate([[0.0], np.cumsum(v)]) for k, v in dict(
        n=np.ones(n), x=x, y=y, xx=x * x, yy=y * y, xy=x * y, z=z, xz=x * z, yz=y * z, zz=z * z).items()}
    eg = np.full((n, kmax + 1), np.inf)
    eb = np.full((n, kmax + 1), np.inf)
    for l in range(2, kmax + 1):
        i = np.arange(0, n - l)
        if len(i) == 0:
            break
        j = i + l + 1
        s = {k: v[j] - v[i] for k, v in S.items()}
        m = s['n']
        mx, my = s['x'] / m, s['y'] / m
        cxx, cyy, cxy = s['xx'] / m - mx * mx, s['yy'] / m - my * my, s['xy'] / m - mx * my
        spur, det = cxx + cyy, cxx * cyy - cxy * cxy
        eg[i, l] = np.maximum(spur / 2 - np.sqrt(np.maximum(spur * spur / 4 - det, 0)), 0) * m
        A = np.stack([np.stack([s['xx'], s['xy'], s['x']], -1),
                      np.stack([s['xy'], s['yy'], s['y']], -1),
                      np.stack([s['x'], s['y'], m], -1)], -2)
        b = -np.stack([s['xz'], s['yz'], s['z']], -1)
        ok = np.abs(np.linalg.det(A)) > 1e-12
        sol = np.zeros_like(b)
        if ok.any():
            sol[ok] = np.linalg.solve(A[ok], b[ok][..., None])[..., 0]
        D, E, F = sol[:, 0], sol[:, 1], sol[:, 2]
        r = np.sqrt(np.maximum(D * D / 4 + E * E / 4 - F, 1e-12))
        rest = (s['zz'] + D * D * s['xx'] + E * E * s['yy'] + F * F * m
                + 2 * D * s['xz'] + 2 * E * s['yz'] + 2 * F * s['z']
                + 2 * D * E * s['xy'] + 2 * D * F * s['x'] + 2 * E * F * s['y'])
        gut = ok & (r >= rmin) & (r <= rmax)
        eb[i[gut], l] = (np.maximum(rest, 0) / (4 * r * r))[gut]
    return eg, eb


def _kreis(P):
    x, y = P[:, 0], P[:, 1]
    D, E, F = np.linalg.lstsq(np.stack([x, y, np.ones_like(x)], 1), -(x * x + y * y), rcond=None)[0]
    c = np.array([-D / 2, -E / 2])
    return c, math.sqrt(max(c[0] * c[0] + c[1] * c[1] - F, 1e-12))


def _richtung(e, p, ende):
    """Laufrichtung eines Elements am Ende bzw. Anfang."""
    if isinstance(e, Gerade):
        return e.d
    c, _ = e.kreis
    n = len(p)
    rad = p[(e.i1 if ende else e.i0) % n] - c
    t = np.array([-rad[1], rad[0]]) / max(math.hypot(*rad), 1e-9)
    anf, mit = p[e.i0 % n] - c, p[((e.i0 + e.i1) // 2) % n] - c
    return t if anf[0] * mit[1] - anf[1] * mit[0] > 0 else -t


def segmentieren(p, ds, par):
    """Gleichmäßig abgetasteten Ring optimal in Geraden und Kreisbögen zerlegen
    (kleinste Summe aus Fehlerquadraten und Preis je Stück). -> (Elemente,
    Modellpunkte): Elemente als Liste (Element, Ecke am Anfang), Modellpunkte
    = jeder Ringpunkt auf seine Gerade bzw. seinen Kreis projiziert."""
    n = len(p)
    kmax = min(n - 1, int(par['seg_max'] / ds))
    # Anfang an der schärfsten Ecke, damit kein Stück über den Anfang läuft
    m = max(2, int(round(0.5 / ds)))
    v1 = p - np.roll(p, m, axis=0)
    v2 = np.roll(p, -m, axis=0) - p
    start = int(np.argmax(np.abs(np.arctan2(v1[:, 0] * v2[:, 1] - v1[:, 1] * v2[:, 0], (v1 * v2).sum(1)))))
    q = np.roll(p, -start, axis=0)
    q = np.vstack([q, q[:1]])
    eg, eb = _kosten(q, kmax, par['r_min'], par['r_max'])
    N = len(q)
    best = np.full(N, np.inf)
    best[0] = 0.0
    wahl = [None] * N
    for j in range(2, N):
        i = np.arange(max(0, j - kmax), j - 1)
        cg = best[i] + eg[i, j - i] + par['lam_gerade']
        cb = best[i] + eb[i, j - i] + par['lam_bogen']
        kg, kb = int(np.argmin(cg)), int(np.argmin(cb))
        best[j], wahl[j] = (cg[kg], (int(i[kg]), 'g')) if cg[kg] <= cb[kb] else (cb[kb], (int(i[kb]), 'b'))
    if wahl[N - 1] is None:
        return None
    teile, j = [], N - 1
    while j > 0:
        i, art = wahl[j]
        teile.append((i, j, art))
        j = i
    el = []
    M = np.zeros_like(p)
    for i, j, art in reversed(teile):
        a, b = i + start, j + start
        P = q[i:j + 1]
        idx = np.arange(a, b + 1) % n
        if art == 'g':
            c, d, _ = _ausgleich(P)
            if np.dot(d, P[-1] - P[0]) < 0:
                d = -d
            el.append(Gerade(a, b, c, d))
            M[idx] = c + ((P - c) @ d)[:, None] * d
        else:
            c, r = _kreis(P)
            el.append(Kurve(a, b, (c, r)))
            v = P - c
            M[idx] = c + r * v / np.maximum(np.hypot(v[:, 0], v[:, 1]), 1e-9)[:, None]
    out = []
    for k, e in enumerate(el):
        vor = el[k - 1]
        dreh = math.degrees(math.acos(max(-1.0, min(1.0, float(np.dot(_richtung(vor, p, True),
                                                                        _richtung(e, p, False)))))))
        if isinstance(e, Kurve) and isinstance(vor, Kurve):
            grenze = par['knick_bogen']
        elif isinstance(e, Gerade) and isinstance(vor, Gerade):
            grenze = par['knick_gerade']
        else:
            grenze = par['knick_stoss']
        out.append((e, dreh > grenze))
    return out, M


def _vieleck_zu_kurve(el, ds, par):
    """Ketten kurzer Geraden mit mäßigen, gleichsinnigen Knicken (eine Rundung
    als Vieleck) -> Kurve."""
    L = len(el)
    if L < 4:
        return el
    kurz = [isinstance(e, Gerade) and _laenge(e, ds) < par['poly_max'] for e, _ in el]
    dreh = []
    for k in range(L):
        a, b = el[k - 1][0], el[k][0]
        if isinstance(a, Gerade) and isinstance(b, Gerade):
            dreh.append(math.degrees(math.atan2(a.d[0] * b.d[1] - a.d[1] * b.d[0], float(np.dot(a.d, b.d)))))
        else:
            dreh.append(None)
    start = next((k for k in range(L) if not kurz[k]), None)
    if start is None:
        return el
    folge = [(start + k) % L for k in range(L)]
    out, k = [], 0
    while k < L:
        i = folge[k]
        j = k
        while j + 1 < L and kurz[folge[j]] and kurz[folge[j + 1]]:
            d = dreh[folge[j + 1]]
            if d is None or not par['poly_knick'][0] <= abs(d) <= par['poly_knick'][1]:
                break
            if j > k and (d > 0) != (dreh[folge[j]] > 0):
                break
            j += 1
        if j - k + 1 >= par['poly_min'] and kurz[i]:
            out.append((Kurve(el[i][0].i0, el[folge[j]][0].i1), el[i][1]))
            k = j + 1
        else:
            out.append(el[i])
            k += 1
    return out


def _tangente(e, p, ds, ende):
    """Tangente am Ende (ende=True) bzw. Anfang einer Kurve als Gerade."""
    k = max(2, int(0.6 / ds))
    Q = _stueck(p, e.i1 - k, e.i1) if ende else _stueck(p, e.i0, e.i0 + k)
    _, d, _ = _ausgleich(Q)
    if np.dot(d, Q[-1] - Q[0]) < 0:
        d = -d
    return Gerade(e.i0, e.i1, Q[-1] if ende else Q[0], d)


def _ersetzen(el, k, m, ersatz, e2):
    """el[k+1 .. k+m-1] (zyklisch) durch ersatz ersetzen; e2 bekommt eine Ecke."""
    L = len(el)
    weg = {(k + j) % L for j in range(1, m)}
    neu = []
    for i in range(L):
        if i in weg:
            if i == (k + 1) % L:
                neu.extend(ersatz)
            continue
        neu.append((e2, True) if el[i][0] is e2 else el[i])
    return neu


def _zusammenfassen(p, el, k, bis, par):
    """el[k] .. el[bis] (zyklisch, beide Geraden) zu einer Geraden, wenn sie passt."""
    L = len(el)
    g1, g2 = el[k][0], el[bis % L][0]
    Q = _stueck(p, g1.i0, g2.i1)
    c, d, abst = _ausgleich(Q)
    if abst.max() > par['max_gerade'] * 1.5:
        return None
    gz = Gerade(g1.i0, g1.i0 + len(Q) - 1, c, d if np.dot(d, Q[-1] - Q[0]) >= 0 else -d)
    gz.fest = True
    weg = {(k + j) % L for j in range(0, bis - k + 1)}
    neu = []
    for i in range(L):
        if i in weg:
            if i == k:
                neu.append((gz, el[k][1]))
            continue
        neu.append(el[i])
    return neu


def ecken_bilden(p, ds, el, par, tinte_links):
    """Zwischen zwei langen Elementen (Geraden; Bögen mit ihrer Tangente):
    rundes Strichende -> gerade Kappe, abgerundete Ecke -> spitze Ecke, fast
    gleiche Geraden -> eine. Danach kurze freie Geraden wieder zu Kurven."""
    n = len(p)
    for e, _ in el:
        if isinstance(e, Gerade) and np.dot(e.d, p[e.i1 % n] - p[e.i0 % n]) < 0:
            e.d = -e.d
    geaendert = True
    while geaendert and len(el) >= 3:
        geaendert = False
        L = len(el)
        lang = [(isinstance(e, Gerade) and (_laenge(e, ds) >= par['kurz_gerade'] or e.fest))
                or (isinstance(e, Kurve) and e.kreis is not None and _laenge(e, ds) >= par['kurve_lang']
                    and e.kreis[1] >= par['kurve_r']) for e, _ in el]
        for k in range(L):
            if not lang[k]:
                continue
            e1 = el[k][0]
            g1 = e1 if isinstance(e1, Gerade) else _tangente(e1, p, ds, True)
            m = 1
            while m < L and not lang[(k + m) % L]:
                m += 1
            if m >= L:
                continue
            e2 = el[(k + m) % L][0]
            if e2 is e1:
                continue
            g2 = e2 if isinstance(e2, Gerade) else _tangente(e2, p, ds, False)
            beide_geraden = e1 is g1 and e2 is g2
            dreh = math.degrees(math.acos(max(-1.0, min(1.0, float(np.dot(g1.d, g2.d))))))
            if m == 1:
                # zwei Geraden hintereinander, fast gleiche Richtung
                if beide_geraden and dreh < par['gleich_grad']:
                    neu = _zusammenfassen(p, el, k, k + 1, par)
                    if neu is not None:
                        el, geaendert = neu, True
                        break
                continue
            i0, i1 = el[(k + 1) % L][0].i0, el[(k + m - 1) % L][0].i1
            P = _stueck(p, i0, i1)
            i1 = i0 + len(P) - 1
            laenge = (len(P) - 1) * ds
            sehne = P[-1] - P[0]
            n_tinte = np.array([-sehne[1], sehne[0]]) * (1 if tinte_links else -1)
            konvex = float(np.dot(P[len(P) // 2] - (P[0] + P[-1]) / 2, n_tinte)) < 0
            ersatz = None
            kw = par['kappe_winkel'] if beide_geraden else par['kappe_winkel_bogen']
            if par['kappen'] and konvex and dreh > kw and laenge <= par['kappe_max']:
                # Strichende: gerade Kappe quer, achsparallel am äußersten Punkt
                s = g1.d - g2.d
                if abs(s[1]) >= abs(s[0]):
                    kap = Gerade(i0, i1, (P[:, 0].mean(), P[:, 1].max() if s[1] > 0 else P[:, 1].min()), (1, 0))
                    kap.achse = 'h'
                else:
                    kap = Gerade(i0, i1, (P[:, 0].max() if s[0] > 0 else P[:, 0].min(), P[:, 1].mean()), (0, 1))
                    kap.achse = 'v'
                if np.dot(kap.d, sehne) < 0:
                    kap.d = -kap.d
                kap.fest = True
                x1, x2 = _schnitt(g1, kap), _schnitt(kap, g2)
                if x1 is not None and x2 is not None and math.hypot(*(x2 - x1)) <= par['kappe_breite'] and \
                        max(np.hypot(*(P - x1).T).min(), np.hypot(*(P - x2).T).min()) <= par['kappe_abstand']:
                    ersatz = [(kap, True)]
            if ersatz is None and laenge <= par['ecke_kurve_max'] and beide_geraden and dreh < par['ecke_winkel']:
                neu = _zusammenfassen(p, el, k, k + m, par)
                if neu is not None:
                    el, geaendert = neu, True
                    break
            if ersatz is None and laenge <= par['ecke_kurve_max'] and \
                    par['ecke_winkel'] <= dreh <= par['kappe_winkel']:
                x = _schnitt(g1, g2)
                if x is not None and np.hypot(*(P - x).T).min() <= par['ecke_abstand']:
                    ersatz = []
            if ersatz is not None:
                el, geaendert = _ersetzen(el, k, m, ersatz, e2), True
                break
    out = []
    for k, (e, f) in enumerate(el):
        if isinstance(e, Gerade) and not f and not el[(k + 1) % len(el)][1] and \
                _laenge(e, ds) < par['min_gerade_frei']:
            e = Kurve(e.i0, e.i1)
        out.append((e, f))
    zus = []
    for e, f in out:
        if zus and isinstance(e, Kurve) and isinstance(zus[-1][0], Kurve) and not f:
            zus[-1] = (Kurve(zus[-1][0].i0, e.i1), zus[-1][1])
        else:
            zus.append((e, f))
    if len(zus) > 1 and isinstance(zus[0][0], Kurve) and isinstance(zus[-1][0], Kurve) and not zus[0][1]:
        zus[-1] = (Kurve(zus[-1][0].i0, zus[0][0].i1 + n), zus[-1][1])
        del zus[0]
    return zus


def aufbauen(p, ds, el, par, M):
    """Elemente -> Punkte des geordneten Rings: Ecken als Schnittpunkte, Kurven
    aus den Modellpunkten geglättet und an verschobene Enden angeglichen."""
    n = len(p)
    E = len(el)
    if E == 1 and isinstance(el[0][0], Kurve):
        return _glaetten(M, par['sigma_kurve'] / ds, True)
    punkte = []
    for k, (e, _) in enumerate(el):
        vor = el[k - 1][0]
        pt = p[e.i0 % n]
        x = None
        if isinstance(vor, Gerade) and isinstance(e, Gerade):
            x = _schnitt(vor, e)
            if x is not None and np.hypot(*(_stueck(p, vor.i0, e.i1) - x).T).min() > par['max_schnitt']:
                x = None
            if x is None:
                x = 0.5 * (vor.proj(pt) + e.proj(pt))
        elif isinstance(vor, Gerade):
            x = vor.proj(pt)
        elif isinstance(e, Gerade):
            x = e.proj(pt)
        else:
            x = pt
        punkte.append(np.asarray(x, float))
    out = []
    for k, (e, _) in enumerate(el):
        out.append(punkte[k])
        if isinstance(e, Gerade):
            continue
        P = _stueck(M, e.i0, e.i1)
        if len(P) >= 3:
            P = _glaetten(P, par['sigma_kurve'] / ds, False)
        d0, d1 = punkte[k] - P[0], punkte[(k + 1) % E] - P[-1]
        s = np.arange(len(P)) * ds
        S = s[-1] if len(s) > 1 else 1.0
        b = par['angleichen']
        if S < 2 * b:
            w0, w1 = 1 - s / S, s / S
        else:
            w0, w1 = np.clip(1 - s / b, 0, 1), np.clip(1 - (S - s) / b, 0, 1)
        out.extend((P + w0[:, None] * d0 + w1[:, None] * d1)[1:-1])
    return np.array(out)


def _spiegeln(F, abtast, rand, achse):
    """Feld an der senkrechten Achse x = achse (Pixel des Ausschnitts) spiegeln
    und mit dem Original mitteln."""
    h, w = F.shape
    x = (np.arange(w) + 0.5) / abtast - rand
    js = (2 * achse - x + rand) * abtast - 0.5
    j0 = np.floor(js).astype(int)
    t = js - j0
    G = F[:, np.clip(j0, 0, w - 1)] * (1 - t) + F[:, np.clip(j0 + 1, 0, w - 1)] * t
    G[:, (js < 0) | (js > w - 1)] = 0
    return 0.5 * (F + G)


def _symmetrie(F, abtast, rand):
    """Beste senkrechte Achse (kleinste Abweichung vom Spiegelbild, in 1/32 px)
    -> (Achse, gemitteltes Feld, Rest relativ zur Fläche)."""
    spalten = np.clip(F - 0.5, 0, None).sum(0)
    xs = (np.arange(F.shape[1]) + 0.5) / abtast - rand
    a0 = float((spalten * xs).sum() / max(spalten.sum(), 1e-9))
    kand = a0 + np.arange(-24, 25) / 32.0
    fehler = [float(((F - _spiegeln(F, abtast, rand, a)) ** 2).sum()) for a in kand]
    achse = float(kand[int(np.argmin(fehler))])
    S = _spiegeln(F, abtast, rand, achse)
    return achse, S, float(np.abs(F - S).sum() * 2 / max((F > 0.5).sum(), 1))


def _tinte_seite(g, ringe):
    """+1, wenn die Tinte auf der Seite der Normale der Geraden liegt, sonst -1."""
    q = g.c + 0.25 * g.normale()
    return 1 if sum(_im_ring(q, r) for r in ringe) % 2 == 1 else -1


def zeichen_nachzeichnen(crop, par, zeichen):
    """Ein Zeichen: Feld, Symmetrie, Kanten, Zerlegung, Ecken/Kappen."""
    F = feld(np.pad(crop, RAND), ABTAST, par['iterationen'], par['unschaerfe'], par['haerte'])
    sym = None
    if zeichen in SYMMETRISCH:
        achse, S, rest = _symmetrie(F, ABTAST, RAND)
        if rest <= par['max_sym_rest']:
            F, sym = S, achse
    roh = [r for r in kanten(F, par['schwelle'], ABTAST, RAND) if abs(flaeche(r)) > 0.02]
    tinte_links = bool(roh) and flaeche(max(roh, key=lambda r: abs(flaeche(r)))) > 0
    ringe = []
    for r in roh:
        el = M = None
        p, ds = r, None
        if len(r) >= 8:
            p, ds = _gleichmaessig(r, par['schritt'])
            p = _glaetten(p, par['sigma_vor'] / ds, True)
            erg = segmentieren(p, ds, par)
            if erg is not None:
                el, M = erg
                el = ecken_bilden(p, ds, _vieleck_zu_kurve(el, ds, par), par, tinte_links)
        ringe.append(dict(p=p, ds=ds, el=el, M=M, roh=r))
    return dict(roh=roh, ringe=ringe, sym=sym)


def _geraden(glyphen, achse=None):
    for g in glyphen:
        for r in g['ringe']:
            for e, _ in r['el'] or []:
                if isinstance(e, Gerade) and (achse is None or e.achse == achse):
                    yield g, r, e


def _achsen_einrasten(glyphen, par):
    """Fast waagerechte/senkrechte Geraden exakt achsparallel (um ihren
    Schwerpunkt gedreht), wenn ihre Enden dabei kaum wandern."""
    for g, r, e in _geraden(glyphen):
        ang = math.degrees(math.atan2(e.d[1], e.d[0])) % 180
        abw = min(ang, 180 - ang, abs(ang - 90))
        if abw >= par['achse_grad'] or _laenge(e, r['ds']) / 2 * math.sin(math.radians(abw)) > par['achse_versatz']:
            continue
        if min(ang, 180 - ang) < par['achse_grad']:
            e.d, e.achse = np.array([1.0 if e.d[0] >= 0 else -1.0, 0.0]), 'h'
        else:
            e.d, e.achse = np.array([0.0, 1.0 if e.d[1] >= 0 else -1.0]), 'v'


def _haufen(werte, gewichte, radius, luecke):
    """1D-Häufung: sortieren, an Lücken > luecke trennen -> [(gewichtete Mitte,
    Indizes im Radius)]."""
    o = list(np.argsort(werte))
    if not o:
        return []
    gruppen, akt = [], [o[0]]
    for a, b in zip(o, o[1:]):
        if werte[b] - werte[a] > luecke:
            gruppen.append(akt)
            akt = []
        akt.append(b)
    gruppen.append(akt)
    out = []
    for gr in gruppen:
        w = np.array([gewichte[i] for i in gr])
        v = np.array([werte[i] for i in gr])
        mitte = float((w * v).sum() / w.sum())
        out.append((mitte, [i for i in gr if abs(werte[i] - mitte) <= radius]))
    return out


def _metriken_einrasten(glyphen, par, protokoll):
    """Waagerechte Kanten auf gemeinsame Höhen in Zeilenkoordinaten (y in der
    Zelle + Versatz), getrennt nach Ober- und Unterkante; nur Häufungen aus
    mindestens metrik_min Zeichen (Grundlinie, Versal-, x-Höhe ...)."""
    for seite in (1, -1):
        items = []
        for g, r, e in _geraden(glyphen, 'h'):
            if _tinte_seite(e, g['roh']) * (1 if e.normale()[1] > 0 else -1) == seite:
                items.append((e.c[1] + g['versatz'], _laenge(e, r['ds']), g, e))
        for mitte, idx in _haufen([i[0] for i in items], [i[1] for i in items],
                                  par['metrik_radius'], par['metrik_luecke']):
            if len({id(items[i][2]) for i in idx}) < par['metrik_min']:
                continue
            for i in idx:
                items[i][3].c = items[i][3].c + np.array([0.0, mitte - items[i][0]])
            protokoll.append(f'Höhe {"oben" if seite > 0 else "unten"} {mitte:.2f}: {len(idx)} Kanten')


def _staerken_einrasten(glyphen, par, achse, protokoll):
    """Gegenüberliegende achsparallele Kanten eines Buchstabens/einer Ziffer
    (Strich) auf gemeinsame Strichstärken, beide Kanten um die Hälfte."""
    k = 0 if achse == 'v' else 1
    paare = []
    for g in glyphen:
        if not g['zeichen'].isalnum():
            continue
        kanten_ = []
        for r in g['ringe']:
            for e, _ in r['el'] or []:
                if isinstance(e, Gerade) and e.achse == achse:
                    lang = _stueck(r['p'], e.i0, e.i1)[:, 1 - k]
                    s = _tinte_seite(e, g['roh']) * (1 if e.normale()[k] > 0 else -1)
                    kanten_.append((e.c[k], lang.min(), lang.max(), s, e))
        for a in kanten_:
            if a[3] != 1:
                continue
            best = None
            for b in kanten_:
                if b[3] != -1 or b[0] <= a[0]:
                    continue
                ueber = min(a[2], b[2]) - max(a[1], b[1])
                if ueber < 0.5 * min(a[2] - a[1], b[2] - b[1]) or ueber < par['staerke_min_laenge']:
                    continue
                if best is None or b[0] < best[0]:
                    best = b
            if best is not None and best[0] - a[0] <= par['staerke_max']:
                paare.append((best[0] - a[0], min(a[2], best[2]) - max(a[1], best[1]), a[4], best[4], g))
    for ziel, idx in _haufen([q[0] for q in paare], [q[1] for q in paare],
                             par['staerke_radius'], par['staerke_luecke']):
        if len({id(paare[i][4]) for i in idx}) < par['staerke_min_n']:
            continue
        for i in idx:
            w, _, ea, eb, _ = paare[i]
            v = np.zeros(2)
            v[k] = (ziel - w) / 2
            ea.c, eb.c = ea.c - v, eb.c + v
        protokoll.append(f'Strich {"senkrecht" if achse == "v" else "waagerecht"} {ziel:.2f}: '
                         f'{"".join(sorted({paare[i][4]["zeichen"] for i in idx}))}')


def _hausdorff(a, b):
    d = np.full(len(a), np.inf)
    for k in range(0, len(b), 256):
        t = b[k:k + 256]
        d = np.minimum(d, np.hypot(a[:, None, 0] - t[None, :, 0], a[:, None, 1] - t[None, :, 1]).min(1))
    return float(d.max())


def _passt(q, roh, par):
    """Geordneter Ring nahe genug am nachgezeichneten (Fläche, größter Abstand)?"""
    fq, fr = flaeche(q), flaeche(roh)
    if fq * fr <= 0 or not 1 - par['val_flaeche'] <= fq / fr <= 1 + par['val_flaeche']:
        return False
    dq, _ = _gleichmaessig(q, 0.05)
    return max(_hausdorff(dq, roh), _hausdorff(roh, dq)) <= par['val_abstand']


def schrift_nachzeichnen(alpha, zellen, par):
    """Alle Zeichen einer Schrift nachzeichnen und ordnen -> (Ringe in
    Blattkoordinaten, Protokollzeilen)."""
    glyphen, protokoll = [], []
    for code, x0, y0, x1, y1, versatz in zellen:
        crop = alpha[y0:y1, x0:x1]
        if crop.max() < 0.02:
            continue
        g = zeichen_nachzeichnen(crop, par, chr(code) if code < 127 else '')
        g.update(code=code, zeichen=chr(code) if code < 127 else f'#{code}', zelle=(x0, y0, x1, y1),
                 versatz=versatz, crop=crop)
        glyphen.append(g)
    _achsen_einrasten(glyphen, par)
    _staerken_einrasten(glyphen, par, 'v', protokoll)
    _staerken_einrasten(glyphen, par, 'h', protokoll)
    _metriken_einrasten(glyphen, par, protokoll)
    ringe, netz, ersatz = [], [], 0
    for g in glyphen:
        x0, y0, x1, y1 = g['zelle']
        neu, alt = [], []
        for r in g['ringe']:
            a = formen_glaetten(r['roh'], sigma=GEMEINSAM['sigma'], schaerfen=True) if len(r['roh']) >= 8 \
                else r['roh']
            q = None if r['el'] is None else aufbauen(r['p'], r['ds'], r['el'], par, r['M'])
            if q is None or not _passt(q, r['roh'], par):
                ersatz += 1
                q = a
            neu.append(_vereinfachen(q, TOLERANZ))
            alt.append(_vereinfachen(a, TOLERANZ))

        def fehler(rs):
            rs = [r for r in rs if len(r) >= 3]
            if not rs:
                return 1.0
            D = fuellen([(rs, True)], x1 - x0, y1 - y0, 1, zeilen=32)
            return math.sqrt(float(np.mean((D - g['crop']) ** 2)))
        f_neu, f_alt = fehler(neu), fehler(alt)
        if f_neu > f_alt * par['netz_faktor'] + par['netz_abstand'] / 255:
            netz.append(f'{g["zeichen"]} ({f_neu * 255:.1f} statt {f_alt * 255:.1f})')
            neu = alt
        for q in neu:
            if len(q) >= 3 and abs(flaeche(q)) > 0.02:
                ringe.append(q + np.array([x0, y0], float))
    sym = ''.join(g['zeichen'] for g in glyphen if g['sym'] is not None)
    protokoll.append(f'spiegelsymmetrisch: {sym}')
    if ersatz:
        protokoll.append(f'{ersatz} Ringe nur geglättet (geordnet zu weit vom Umriss)')
    if netz:
        protokoll.append('Sicherheitsnetz (Zeichen nur geglättet): ' + ', '.join(netz))
    return ringe, protokoll


def svg_schreiben(pfad, name, gruppen, w, h, alpha, faktor):
    def zahl(v):
        s = f'{v:.3f}'.rstrip('0').rstrip('.')
        return s if s != '-0' else '0'
    pfade = []
    for g in gruppen:
        d = []
        for ring in g:
            d.append('M' + ' '.join(f'{zahl(x)},{zahl(y)}' for x, y in ring) + 'Z')
        pfade.append(f'    <path d="{" ".join(d)}"/>')
    vorlage = Image.fromarray(np.clip(alpha * 255, 0, 255).astype(np.uint8), 'L')
    png = Image.new('LA', vorlage.size, 0)
    png.putalpha(vorlage)
    buf = io.BytesIO()
    png.save(buf, format='PNG')
    b64 = base64.b64encode(buf.getvalue()).decode()
    text = f'''<?xml version="1.0" encoding="UTF-8"?>
<!-- Zeitbrecher Zwo: Schrift "{name}", aus game/ts2/textures/misc/{name}.rgb
     nachgezeichnet von tools/hd/make_fonts.py. Aus Spieldaten abgeleitet:
     nie ins Repository. Einheit: Pixel des Originals ({w}x{h}), Zeichen
     aufrecht. Gezeichnet wird jeder sichtbare Pfad (Füllung, keine Linien);
     die Ebene "vorlage" (Original) ist ausgeblendet und wird nie gezeichnet.
     Zeichen nicht verschieben: TS2 liest sie an festen Stellen. -->
<svg xmlns="{SVG_NS}" xmlns:xlink="{XLINK_NS}" width="{w * faktor}" height="{h * faktor}" viewBox="0 0 {w} {h}">
  <g id="vorlage" style="display:none" opacity="0.6">
    <image width="{w}" height="{h}" style="image-rendering:pixelated" xlink:href="data:image/png;base64,{b64}"/>
  </g>
  <g id="zeichen" fill="#000000" fill-rule="evenodd">
{chr(10).join(pfade)}
  </g>
</svg>
'''
    os.makedirs(os.path.dirname(pfad), exist_ok=True)
    with open(pfad, 'w', encoding='utf-8') as f:
        f.write(text)


# ======================================================================
# SVG lesen (Teilmenge: g, path, polygon, rect; transform; display:none)
# ======================================================================

_ZAHL = re.compile(r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?')
_TOKEN = re.compile(r'([MmLlHhVvCcSsQqTtAaZz])|([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)')


def _matrix_mal(m, n):
    a, b, c, d, e, f = m
    a2, b2, c2, d2, e2, f2 = n
    return (a * a2 + c * b2, b * a2 + d * b2, a * c2 + c * d2, b * c2 + d * d2,
            a * e2 + c * f2 + e, b * e2 + d * f2 + f)


def _transform(text):
    m = (1, 0, 0, 1, 0, 0)
    for art, args in re.findall(r'(\w+)\s*\(([^)]*)\)', text or ''):
        v = [float(x) for x in _ZAHL.findall(args)]
        if art == 'matrix' and len(v) == 6:
            n = tuple(v)
        elif art == 'translate':
            n = (1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0)
        elif art == 'scale':
            n = (v[0], 0, 0, v[1] if len(v) > 1 else v[0], 0, 0)
        elif art == 'rotate':
            r = math.radians(v[0])
            n = (math.cos(r), math.sin(r), -math.sin(r), math.cos(r), 0, 0)
            if len(v) == 3:
                n = _matrix_mal(_matrix_mal((1, 0, 0, 1, v[1], v[2]), n), (1, 0, 0, 1, -v[1], -v[2]))
        elif art == 'skewX':
            n = (1, 0, math.tan(math.radians(v[0])), 1, 0, 0)
        elif art == 'skewY':
            n = (1, math.tan(math.radians(v[0])), 0, 1, 0, 0)
        else:
            continue
        m = _matrix_mal(m, n)
    return m


def _stil(el):
    s = {}
    for teil in (el.get('style') or '').split(';'):
        if ':' in teil:
            k, v = teil.split(':', 1)
            s[k.strip()] = v.strip()
    for k in ('display', 'visibility', 'fill', 'fill-rule'):
        if el.get(k) is not None and k not in s:
            s[k] = el.get(k)
    return s


def _kurve(p0, p1, p2, p3, n):
    t = np.linspace(0, 1, n + 1)[1:, None]
    return ((1 - t) ** 3 * p0 + 3 * (1 - t) ** 2 * t * p1 + 3 * (1 - t) * t ** 2 * p2 + t ** 3 * p3)


def _schritte(*pts, fein=1.0):
    laenge = sum(math.hypot(*(np.subtract(b, a))) for a, b in zip(pts, pts[1:]))
    return max(2, min(64, int(math.ceil(laenge * fein))))


def _bogen(p0, rx, ry, phi, gross, dreh, p1, fein):
    """SVG-Bogen (Endpunkt-Form) als Punkte ohne p0."""
    if rx == 0 or ry == 0:
        return [np.array(p1, float)]
    rx, ry = abs(rx), abs(ry)
    cp, sp = math.cos(math.radians(phi)), math.sin(math.radians(phi))
    dx, dy = (p0[0] - p1[0]) / 2, (p0[1] - p1[1]) / 2
    x1, y1 = cp * dx + sp * dy, -sp * dx + cp * dy
    lam = (x1 / rx) ** 2 + (y1 / ry) ** 2
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    zaehler = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1
    k = math.sqrt(max(0.0, zaehler / (rx * rx * y1 * y1 + ry * ry * x1 * x1)))
    if gross == dreh:
        k = -k
    cx1, cy1 = k * rx * y1 / ry, -k * ry * x1 / rx
    cx = cp * cx1 - sp * cy1 + (p0[0] + p1[0]) / 2
    cy = sp * cx1 + cp * cy1 + (p0[1] + p1[1]) / 2

    def winkel(ux, uy, vx, vy):
        return math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
    t1 = winkel(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry)
    dt = winkel((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry)
    if not dreh and dt > 0:
        dt -= 2 * math.pi
    elif dreh and dt < 0:
        dt += 2 * math.pi
    n = max(4, min(128, int(abs(dt) * max(rx, ry) * fein) + 1))
    t = t1 + dt * np.linspace(0, 1, n + 1)[1:]
    x = cx + rx * np.cos(t) * cp - ry * np.sin(t) * sp
    y = cy + rx * np.cos(t) * sp + ry * np.sin(t) * cp
    return list(np.stack([x, y], 1))


def pfad_ringe(d, fein):
    """SVG-Pfaddaten -> Liste geschlossener Punktlisten (offene werden geschlossen)."""
    tokens = [(m.group(1), m.group(2)) for m in _TOKEN.finditer(d)]
    i = 0
    ringe, ring = [], []
    cur = np.zeros(2)
    start = np.zeros(2)
    letzte_ctrl = None
    cmd = None

    def zahlen(n):
        nonlocal i
        v = []
        while len(v) < n:
            if i >= len(tokens) or tokens[i][1] is None:
                raise ValueError('Pfad: Zahl erwartet')
            v.append(float(tokens[i][1]))
            i += 1
        return v

    def abschliessen():
        nonlocal ring
        if len(ring) >= 3:
            ringe.append(np.array(ring, float))
        ring = []

    while i < len(tokens):
        if tokens[i][0]:
            cmd = tokens[i][0]
            i += 1
        elif cmd is None:
            raise ValueError('Pfad beginnt ohne Befehl')
        rel = cmd.islower()
        C = cmd.upper()
        basis = cur if rel else np.zeros(2)
        if C == 'Z':
            abschliessen()
            cur = start.copy()
            letzte_ctrl = None
            continue
        if C == 'M':
            abschliessen()
            cur = basis + zahlen(2)
            start = cur.copy()
            ring = [cur.copy()]
            cmd = 'l' if rel else 'L'  # weitere Paare sind Linien
            letzte_ctrl = None
        elif C == 'L':
            cur = basis + zahlen(2)
            ring.append(cur.copy())
            letzte_ctrl = None
        elif C == 'H':
            x = zahlen(1)[0]
            cur = np.array([cur[0] + x if rel else x, cur[1]])
            ring.append(cur.copy())
            letzte_ctrl = None
        elif C == 'V':
            y = zahlen(1)[0]
            cur = np.array([cur[0], cur[1] + y if rel else y])
            ring.append(cur.copy())
            letzte_ctrl = None
        elif C in 'CS':
            if C == 'C':
                v = zahlen(6)
                p1 = basis + v[0:2]
                p2, p3 = basis + v[2:4], basis + v[4:6]
            else:
                v = zahlen(4)
                p1 = 2 * cur - letzte_ctrl[0] if letzte_ctrl is not None and letzte_ctrl[1] == 'C' else cur.copy()
                p2, p3 = basis + v[0:2], basis + v[2:4]
            ring.extend(_kurve(cur, p1, p2, p3, _schritte(cur, p1, p2, p3, fein=fein)))
            cur = p3
            letzte_ctrl = (p2, 'C')
        elif C in 'QT':
            if C == 'Q':
                v = zahlen(4)
                q, p3 = basis + v[0:2], basis + v[2:4]
            else:
                v = zahlen(2)
                q = 2 * cur - letzte_ctrl[0] if letzte_ctrl is not None and letzte_ctrl[1] == 'Q' else cur.copy()
                p3 = basis + v[0:2]
            p1, p2 = cur + 2 / 3 * (q - cur), p3 + 2 / 3 * (q - p3)
            ring.extend(_kurve(cur, p1, p2, p3, _schritte(cur, q, p3, fein=fein)))
            cur = p3
            letzte_ctrl = (q, 'Q')
        elif C == 'A':
            rx, ry, phi, gross, dreh, x, y = zahlen(7)
            p3 = basis + [x, y]
            ring.extend(_bogen(cur, rx, ry, phi, bool(gross), bool(dreh), p3, fein))
            cur = p3
            letzte_ctrl = None
    abschliessen()
    return ringe


def svg_lesen(pfad, fein=4.0):
    """-> (Breite, Höhe des viewBox, Liste (Ringe, evenodd)) in viewBox-Einheiten."""
    wurzel = ET.parse(pfad).getroot()
    vb = [float(x) for x in _ZAHL.findall(wurzel.get('viewBox') or '')]
    if len(vb) != 4:
        raise SystemExit(f'{pfad}: viewBox fehlt')
    m0 = (1, 0, 0, 1, -vb[0], -vb[1])
    formen = []

    def lauf(el, m, regel):
        tag = el.tag.split('}')[-1]
        s = _stil(el)
        if s.get('display') == 'none' or s.get('visibility') == 'hidden' or el.get('id') == 'vorlage':
            return
        if tag in ('image', 'text', 'defs', 'metadata', 'clipPath', 'mask', 'title', 'desc'):
            return
        m = _matrix_mal(m, _transform(el.get('transform')))
        regel = s.get('fill-rule', regel)
        if tag in ('path', 'polygon', 'rect') and s.get('fill', '') != 'none':
            if tag == 'path':
                ringe = pfad_ringe(el.get('d') or '', fein)
            elif tag == 'polygon':
                v = [float(x) for x in _ZAHL.findall(el.get('points') or '')]
                ringe = [np.array(v, float).reshape(-1, 2)] if len(v) >= 6 else []
            else:
                x, y = float(el.get('x', 0)), float(el.get('y', 0))
                rw, rh = float(el.get('width', 0)), float(el.get('height', 0))
                ringe = [np.array([[x, y], [x + rw, y], [x + rw, y + rh], [x, y + rh]], float)]
            a, b, c, d, e, f = m
            ringe = [np.stack([a * r[:, 0] + c * r[:, 1] + e, b * r[:, 0] + d * r[:, 1] + f], 1) for r in ringe]
            if ringe:
                formen.append((ringe, regel == 'evenodd'))
        for kind in el:
            lauf(kind, m, regel)

    lauf(wurzel, m0, 'nonzero')
    return vb[2], vb[3], formen


# ======================================================================
# Füllen mit genauer Deckung (waagerecht exakt, senkrecht ZEILEN Abtastungen)
# ======================================================================

def fuellen(formen, w, h, skala, zeilen=ZEILEN):
    """Formen (Einheit: Original-Pixel) in ein Deckungsbild w*skala x h*skala zeichnen."""
    W, H = int(round(w * skala)), int(round(h * skala))
    D = np.zeros((H, W + 2), np.float64)
    for ringe, evenodd in formen:
        x0s, y0s, x1s, y1s = [], [], [], []
        for r in ringe:
            p = r * skala
            q = np.roll(p, -1, axis=0)
            x0s.append(p[:, 0]); y0s.append(p[:, 1]); x1s.append(q[:, 0]); y1s.append(q[:, 1])
        x0, y0, x1, y1 = map(np.concatenate, (x0s, y0s, x1s, y1s))
        richt = np.where(y1 > y0, 1, -1)
        ymin, ymax = np.minimum(y0, y1), np.maximum(y0, y1)
        # Abtastzeilen mit Mitte (i + 0,5)/zeilen in [ymin, ymax)
        i0 = np.ceil(ymin * zeilen - 0.5).astype(np.int64)
        i1 = np.ceil(ymax * zeilen - 0.5).astype(np.int64)
        i0 = np.clip(i0, 0, H * zeilen)
        i1 = np.clip(i1, 0, H * zeilen)
        n = np.maximum(i1 - i0, 0)
        if n.sum() == 0:
            continue
        kante = np.repeat(np.arange(len(n)), n)
        versatz = np.arange(n.sum()) - np.repeat(np.cumsum(n) - n, n)
        zeile = i0[kante] + versatz
        ys = (zeile + 0.5) / zeilen
        t = (ys - y0[kante]) / (y1[kante] - y0[kante])
        xs = x0[kante] + t * (x1[kante] - x0[kante])
        ordnung = np.lexsort((xs, zeile))
        zeile, xs, wind = zeile[ordnung], xs[ordnung], richt[kante][ordnung]
        windung = np.cumsum(wind)  # je Zeile Summe 0 (geschlossene Ringe)
        innen = (windung % 2 != 0) if evenodd else (windung != 0)
        gueltig = innen[:-1] & (zeile[:-1] == zeile[1:])
        xa = np.clip(xs[:-1][gueltig], 0, W)
        xb = np.clip(xs[1:][gueltig], 0, W)
        zr = zeile[:-1][gueltig] // zeilen
        gewicht = 1.0 / zeilen
        for x, vz in ((xa, 1.0), (xb, -1.0)):
            fl = np.floor(x)
            fr = x - fl
            fl = fl.astype(np.int64)
            np.add.at(D, (zr, fl), vz * gewicht * (1 - fr))
            np.add.at(D, (zr, fl + 1), vz * gewicht * fr)
    return np.clip(np.cumsum(D, axis=1)[:, :W], 0.0, 1.0)


# ======================================================================
# Ablauf
# ======================================================================

def svg_pfad(spiel, name):
    return os.path.join(spiel, 'hd_eigen', 'schrift', name + '.svg')


def textur_pfad(spiel, name, wurzel='hd_eigen'):
    return os.path.join(spiel, wurzel, MISC, name + '.rgb')


def svg_anlegen(spiel, name, faktor, einfach=False):
    alpha, (w, h) = original_alpha(spiel, name)
    zellen = None if einfach else zeichentabelle(spiel, name)
    if zellen is None:
        feld_par = {k: SCHRIFTEN[name][k] for k in ('unschaerfe', 'haerte')}
        ringe = nachzeichnen(alpha, **GEMEINSAM, **feld_par)
        protokoll = ['einfaches Verfahren (nur geglättet)']
    else:
        par = {**GEMEINSAM, **ORDNEN, **SCHRIFTEN[name]}
        ringe, protokoll = schrift_nachzeichnen(alpha, zellen, par)
    gruppen = gruppieren(ringe)
    svg_schreiben(svg_pfad(spiel, name), name, gruppen, w, h, alpha, faktor)
    print(f'  {name}: {len(ringe)} Umrisse in {len(gruppen)} Pfaden -> {svg_pfad(spiel, name)}')
    for zeile in protokoll:
        print(f'    {zeile}')


def zeichnen(spiel, name, faktor):
    _, (w, h) = original_alpha(spiel, name)
    vw, vh, formen = svg_lesen(svg_pfad(spiel, name), fein=faktor * 2.0)
    if (vw, vh) != (w, h):
        # viewBox in anderer Einheit: auf Original-Pixel umrechnen
        formen = [([r * (w / vw, h / vh) for r in ringe], eo) for ringe, eo in formen]
    deckung = fuellen(formen, w, h, faktor)
    zellen = zeichentabelle(spiel, name)
    if zellen is not None:
        # nur innerhalb der Zeichenzellen: das Spiel filtert bilinear über den
        # Zellrand hinaus, Überlauf in die Lücken würde als Saum sichtbar
        maske = np.zeros_like(deckung)
        for _, x0, y0, x1, y1, _ in zellen:
            maske[y0 * faktor:y1 * faktor, x0 * faktor:x1 * faktor] = 1.0
        deckung = deckung * maske
    im = textur_aus_deckung(deckung)
    sgi_speichern(im, textur_pfad(spiel, name))
    print(f'  {name}: {w}x{h} -> {im.width}x{im.height} ({len(formen)} Pfade) -> {textur_pfad(spiel, name)}')
    return deckung


def messen(spiel, name):
    """Neue Textur auf Originalgröße verkleinert (Mittel je Block) gegen das Original."""
    alpha, (w, h) = original_alpha(spiel, name)
    im = Image.open(textur_pfad(spiel, name))
    a = np.flipud(np.asarray(im.getchannel('A'), np.float64) / 255.0)
    k = im.width // w
    klein = a.reshape(h, k, w, k).mean(axis=(1, 3))
    rms = math.sqrt(float(np.mean((klein - alpha) ** 2))) * 255
    tinte = klein.sum() / max(alpha.sum(), 1e-9)
    print(f'  {name}: {im.width}x{im.height}, RMS {rms:.2f} von 255, Tinte {tinte * 100:.1f} % des Originals')
    zellen = zeichentabelle(spiel, name)
    if zellen:
        je = sorted(((math.sqrt(float(np.mean((klein[y0:y1, x0:x1] - alpha[y0:y1, x0:x1]) ** 2))) * 255,
                      chr(c) if c < 127 else f'#{c}') for c, x0, y0, x1, y1, _ in zellen), reverse=True)
        print('    größte Abweichung: ' + ', '.join(f'{z} {e:.1f}' for e, z in je[:8]))


def vergleich(spiel, namen, ordner, faktor):
    """Original (bilinear vergrößert, wie das Spiel filtert) neben der neuen Textur."""
    os.makedirs(ordner, exist_ok=True)
    for name in namen:
        alpha, (w, h) = original_alpha(spiel, name)
        neu = Image.open(textur_pfad(spiel, name))
        a_neu = np.flipud(np.asarray(neu.getchannel('A')))
        alt = Image.fromarray((alpha * 255).astype(np.uint8)).resize((neu.width, neu.height), Image.BILINEAR)
        hinter = (30, 40, 70)
        bilder = []
        for a in (np.asarray(alt), a_neu):
            m = a.astype(np.float32)[..., None] / 255
            rgb = np.array(hinter, np.float32) * (1 - m) + 255 * m
            bilder.append(Image.fromarray(rgb.astype(np.uint8)))
        blatt = Image.new('RGB', (neu.width * 2 + 8, neu.height + 20), (16, 16, 16))
        d = ImageDraw.Draw(blatt)
        d.text((4, 4), f'{name}: Original {w}x{h} bilinear', fill=(255, 255, 160))
        d.text((neu.width + 12, 4), f'neu {neu.width}x{neu.height}', fill=(255, 255, 160))
        blatt.paste(bilder[0], (0, 20))
        blatt.paste(bilder[1], (neu.width + 8, 20))
        p = os.path.join(ordner, f'schrift_{name}.png')
        blatt.save(p)
        print(p)


def _make_hd():
    """installieren/zuruecksetzen aus make_hd.py (gleiche Sicherungsregeln)."""
    pfad = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'make_hd.py')
    spec = importlib.util.spec_from_file_location('make_hd', pfad)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--spiel', default=os.path.join(REPO, 'game'), help='game-Ordner (Standard: <Repo>/game)')
    ap.add_argument('--nur', nargs='*', default=[], help='nur diese Schriften (impact, book, booklight)')
    ap.add_argument('--faktor', type=int, default=FAKTOR, help=f'Vergrößerung (Standard {FAKTOR})')
    ap.add_argument('--svg', action='store_true', help='SVG neu aus dem Original nachzeichnen (überschreibt)')
    ap.add_argument('--einfach', action='store_true', help='mit --svg: nur glätten, nicht ordnen (früheres Verfahren)')
    ap.add_argument('--messen', action='store_true', help='Abweichung der neuen Texturen vom Original')
    ap.add_argument('--vergleich', metavar='ORDNER', help='Vergleichsbilder schreiben')
    ap.add_argument('--installieren', action='store_true', help='hd_eigen nach hd kopieren (Sicherung hd_backup)')
    ap.add_argument('--zuruecksetzen', action='store_true', help='hd aus hd_backup wiederherstellen')
    args = ap.parse_args()
    spiel = os.path.abspath(args.spiel)
    namen = [n for n in SCHRIFTEN if not args.nur or n in args.nur]
    if not namen:
        sys.exit(f'unbekannte Schrift; bekannt: {", ".join(SCHRIFTEN)}')
    if not os.path.isdir(os.path.join(spiel, MISC)):
        sys.exit(f'keine Originaltexturen unter {os.path.join(spiel, MISC)}')
    if args.installieren or args.zuruecksetzen:
        hd = _make_hd()
        muster = ['misc/' + n for n in namen]
        return (hd.installieren if args.installieren else hd.zuruecksetzen)(spiel, muster)
    if args.messen:
        for n in namen:
            messen(spiel, n)
        return
    if args.vergleich:
        return vergleich(spiel, namen, args.vergleich, args.faktor)
    for n in namen:
        if args.svg or not os.path.exists(svg_pfad(spiel, n)):
            svg_anlegen(spiel, n, args.faktor, args.einfach)
        zeichnen(spiel, n, args.faktor)
    print('Fertig. Einspielen: python -I tools/hd/make_fonts.py --installieren '
          '(braucht ts2.exe mit Schrift-Umrechnung, src/host/fontfix.cpp)')


if __name__ == '__main__':
    main()
