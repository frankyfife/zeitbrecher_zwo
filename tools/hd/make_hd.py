"""Eigene hochaufgelöste Texturen für Zeitbrecher Zwo (TS2-PC-Port).

Liest die Originaltexturen aus game/ts2/textures (SGI, .rgb) und schreibt
eigene HD-Fassungen nach game/hd_eigen/ts2/textures/... Zwei Verfahren:

* Vektor: einfache HUD-Formen (Fadenkreuze, Lebensbogen) werden mit
  Abstandsfeldern neu gezeichnet, in 4facher Auflösung und kantenglatt.
  Die Maße (Radien, Linienbreiten, Deckkraft) sind am Original gemessen:
  ``--anpassen`` passt sie so an, dass das verkleinerte Ergebnis dem
  Original möglichst gleicht, ``--messen`` zeigt die Abweichung.
* Skalieren: fotoartige Texturen (Kiste, Riffelblech, Zaun, Baum, Symbole,
  Banner) werden mit Lanczos vergrößert und leicht nachgeschärft; der
  Alphakanal wird getrennt vergrößert und bei Ausschnitt-Texturen
  aufgesteilt. Kachelnde Texturen werden vorher umlaufend aufgefüllt, damit
  die Kanten nahtlos bleiben.

ts2.exe liest Dateien unter game/hd/ vor den Originalen (docs/hd-texturen.md).
Spieldaten und daraus erzeugte Bilder werden nie committet.

Aufrufe (aus dem Repo-Ordner, Python mit Pillow und numpy):

  python -I tools/hd/make_hd.py                 alle Ziele nach game/hd_eigen
  python -I tools/hd/make_hd.py --nur "crosshair_*"
  python -I tools/hd/make_hd.py --liste "misc/gmod_*"
  python -I tools/hd/make_hd.py --vergleich <Ordner>    Vergleichsbilder (PNG)
  python -I tools/hd/make_hd.py --installieren [--nur ...]   hd_eigen -> game/hd
  python -I tools/hd/make_hd.py --zuruecksetzen [--nur ...]  game/hd aus game/hd_backup
  python -I tools/hd/make_hd.py --messen | --anpassen   (Vektor-Ziele)
"""
import argparse
import filecmp
import fnmatch
import os
import shutil
import sys

import numpy as np
from PIL import Image, ImageDraw

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEX = os.path.join('ts2', 'textures')

# ======================================================================
# Abstandsfelder (Einheit: Pixel des Originals, y nach unten, Winkel in
# Grad wie atan2(dy, dx), also 90 = unten). Negativ = innen.
# ======================================================================


def _box(q1, q2):
    """Abstand eines Rechtecks aus den beiden Achsabständen."""
    return np.hypot(np.maximum(q1, 0), np.maximum(q2, 0)) + np.minimum(np.maximum(q1, q2), 0)


def strecke(X, Y, x0, y0, x1, y1, w, rund=True):
    vx, vy = x1 - x0, y1 - y0
    L = max(np.hypot(vx, vy), 1e-9)
    ux, uy = vx / L, vy / L
    px, py = X - x0, Y - y0
    t = px * ux + py * uy
    n = py * ux - px * uy
    if rund:
        return np.hypot(t - np.clip(t, 0, L), n) - w / 2
    return _box(np.abs(t - L / 2) - L / 2, np.abs(n) - w / 2)


def rechteck(X, Y, x0, y0, x1, y1):
    return _box(np.abs(X - (x0 + x1) / 2) - abs(x1 - x0) / 2, np.abs(Y - (y0 + y1) / 2) - abs(y1 - y0) / 2)


def scheibe(X, Y, cx, cy, r):
    return np.hypot(X - cx, Y - cy) - r


def bogen(X, Y, cx, cy, r, w, mitte, halb, rund=False):
    """Kreisbogen-Strich um Winkel ``mitte`` ± ``halb`` (Grad)."""
    dx, dy = X - cx, Y - cy
    rho = np.hypot(dx, dy)
    dl = np.abs((np.arctan2(dy, dx) - np.radians(mitte) + np.pi) % (2 * np.pi) - np.pi) - np.radians(halb)
    dr = np.abs(rho - r) - w / 2
    if rund:
        ends = [(cx + r * np.cos(np.radians(mitte + s * halb)), cy + r * np.sin(np.radians(mitte + s * halb))) for s in (-1, 1)]
        de = np.minimum(*[np.hypot(X - ex, Y - ey) for ex, ey in ends]) - w / 2
        return np.where(dl <= 0, dr, de)
    da = np.where(dl > 0, rho * np.sin(np.clip(dl, 0, np.pi / 2)), dl * rho)
    return _box(dr, da)


def ellipse(X, Y, cx, cy, a, b):
    """Näherung des Abstands zu einer achsparallelen Ellipse (negativ innen)."""
    ex, ey = (X - cx) / a, (Y - cy) / b
    q = np.sqrt(ex * ex + ey * ey) + 1e-12
    g = np.hypot(ex / (a * q), ey / (b * q)) + 1e-12
    return (q - 1) / g


class Leinwand:
    """Raster der Ausgabe: S Ausgabepixel je Originalpixel."""

    def __init__(self, w, h, S):
        self.w, self.h, self.S = w, h, S
        xs = (np.arange(w * S) + 0.5) / S
        ys = (np.arange(h * S) + 0.5) / S
        self.X, self.Y = np.meshgrid(xs, ys)

    def deck(self, d, weich=0.0):
        """Deckung aus Abstand: Kante über 1 Ausgabepixel (oder ``weich``)."""
        return np.clip(0.5 - d / max(weich, 1.0 / self.S), 0, 1)

    def unschaerfe(self, a, sigma):
        return gauss(a, sigma * self.S)


def gauss(a, sigma, modus='edge'):
    """Trennbarer Gaußfilter (sigma in Pixeln des Arrays)."""
    if sigma <= 0.05:
        return a
    r = max(1, int(np.ceil(sigma * 3)))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    p = np.pad(a, ((r, r), (0, 0)), mode=modus)
    a = np.lib.stride_tricks.sliding_window_view(p, 2 * r + 1, axis=0) @ k
    p = np.pad(a, ((0, 0), (r, r)), mode=modus)
    return np.lib.stride_tricks.sliding_window_view(p, 2 * r + 1, axis=1) @ k


def vereinigung(*teile):
    out = teile[0]
    for t in teile[1:]:
        out = np.maximum(out, t)
    return out

# ======================================================================
# Vektor-Modelle. Jedes bekommt die Leinwand und Parameter (Dict) und
# liefert die Deckung (0..1) als Array in Ausgabegröße. Die Startwerte
# stehen in VEKTOR[...]['p'] und sind mit --anpassen gemessen.
# ======================================================================


def _mitte(L, p):
    """Mittelpunkt: beim Messen frei (mx, my), bei der Ausgabe Texturmitte."""
    return p.get('mx', L.w / 2), p.get('my', L.h / 2)


def m_old(L, p):
    X, Y = L.X, L.Y
    cx, cy = _mitte(L, p)
    lx, ly = p.get('lx', cx), p.get('ly', cy)  # Linien (Original: halbes Pixel versetzt)
    teile = [L.deck(np.abs(np.hypot(X - cx, Y - cy) - p['r']) - p['w'] / 2)]
    teile.append(L.deck(rechteck(X, Y, lx - p['lw'] / 2, -1, lx + p['lw'] / 2, L.h + 1)))
    teile.append(L.deck(rechteck(X, Y, -1, ly - p['lw'] / 2, L.w + 1, ly + p['lw'] / 2)))
    tw = p['tw'] / 2  # Marken vom Rand bis zum Ring
    for d in (rechteck(X, Y, lx - tw, -1, lx + tw, cy - p['r']), rechteck(X, Y, lx - tw, cy + p['r'], lx + tw, L.h + 1),
              rechteck(X, Y, -1, ly - tw, cx - p['r'], ly + tw), rechteck(X, Y, cx + p['r'], ly - tw, L.w + 1, ly + tw)):
        teile.append(L.deck(d))
    return vereinigung(*teile)


def m_olde(L, p):
    X, Y = L.X, L.Y
    cx, cy = _mitte(L, p)
    teile = []
    for m in (0, 90, 180, 270):
        teile.append(L.deck(bogen(X, Y, cx, cy, p['R'], p['Rw'], m, p['span'], rund=True)))
    # Kreuz zwischen kleinem Ring und großen Bögen: heller Kern + Schein
    for (ux, uy) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        a0, a1 = p['cr'], p['R']
        seg = (cx + ux * a0, cy + uy * a0, cx + ux * a1, cy + uy * a1)
        teile.append(L.deck(strecke(X, Y, *seg, p['lw'], rund=False)))
        teile.append(p['hI'] * L.deck(strecke(X, Y, *seg, p['hw'], rund=False), p['hs']))
    teile.append(p['cI'] * L.deck(np.abs(np.hypot(X - cx, Y - cy) - p['cr']) - p['cw'] / 2))
    return vereinigung(*teile)


def m_shaded(L, p):
    X, Y = L.X, L.Y
    cx, cy = _mitte(L, p)
    teile = [L.deck(bogen(X, Y, cx, cy, p['R'], p['w'], m, p['span'], rund=True)) for m in (0, 180)]
    rho = np.hypot(X - cx, Y - cy)
    ring = L.deck(rho - p['ra'], p['sa']) * L.deck(p['ri'] - rho, p['si'])
    teile.append(p['I'] * ring)
    return vereinigung(*teile)


def m_triangle(L, p):
    X, Y = L.X, L.Y
    cx, cy = _mitte(L, p)
    teile = []
    for m in (90, 210, 330):
        ux, uy = np.cos(np.radians(m)), np.sin(np.radians(m))
        teile.append(L.deck(strecke(X, Y, cx + ux * p['r0'], cy + uy * p['r0'], cx + ux * p['r'], cy + uy * p['r'], p['lw'], rund=False)))
        teile.append(L.deck(bogen(X, Y, cx, cy, p['r'], p['aw'], m, p['span'], rund=True)))
    return vereinigung(*teile)


def m_circle(L, p):
    X, Y = L.X, L.Y
    cx, cy = _mitte(L, p)
    teile = [L.deck(scheibe(X, Y, cx, cy, p['rd']))]
    for m in (45, 135, 225, 315):
        for i in '123':
            teile.append(p['I' + i] * L.deck(bogen(X, Y, cx, cy, p['r' + i], p['w' + i], m, p['span' + i], rund=True)))
    return vereinigung(*teile)


def m_arrow(L, p):
    """Graustufen auf Schwarz (deckend, wird im Spiel addiert)."""
    X, Y = L.X, L.Y
    cx, cy = _mitte(L, p)
    aussen = []
    for s in (-1, 1):  # Pfeile links und rechts, Spitze zur Mitte
        tip = (cx + s * p['tx'], cy)
        for t in (-1, 1):
            aussen.append(L.deck(strecke(X, Y, cx + s * p['ex'], cy + t * p['ey'], *tip, p['aw'])))
    aussen = vereinigung(*aussen)
    innen = []
    # vier graue Winkel, Spitze zur Mitte (seitlich näher als oben/unten)
    for (ux, uy, t, l) in ((1, 0, p['th'], p['lh']), (-1, 0, p['th'], p['lh']), (0, 1, p['tv'], p['lv']), (0, -1, p['tv'], p['lv'])):
        tx, ty = cx + ux * t, cy + uy * t
        for s in (-1, 1):
            vx, vy = (ux - s * uy) / np.sqrt(2), (uy + s * ux) / np.sqrt(2)
            innen.append(L.deck(strecke(X, Y, tx, ty, tx + vx * l, ty + vy * l, p['iw'], rund=False)))
    punkt = L.deck(rechteck(X, Y, cx - p['pd'], cy - p['pd'], cx + p['pd'], cy + p['pd']))
    schein = p['g'] * L.unschaerfe(aussen, p['gs'])
    return np.clip(vereinigung(aussen, p['iI'] * vereinigung(*innen), punkt, schein), 0, 1)


def m_arc_bar(L, p):
    d = np.maximum(ellipse(L.X, L.Y, p['cx1'], p['cy1'], p['a1'], p['b1']),
                   -ellipse(L.X, L.Y, p['cx2'], p['cy2'], p['a2'], p['b2']))
    return L.deck(d)


def m_arc_outline(L, p):
    X, Y = L.X, L.Y
    d1 = np.abs(ellipse(X, Y, p['cx1'], p['cy1'], p['a1'], p['b1'])) - p['w'] / 2
    d2 = np.abs(ellipse(X, Y, p['cx2'], p['cy2'], p['a2'], p['b2'])) - p['w'] / 2
    aus = np.clip((p['f1'] - X) / max(p['f1'] - p['f0'], 1e-3), 0, 1)  # Ausblenden nach rechts oben
    return vereinigung(L.deck(d1), L.deck(d2)) * aus


# Kanal: 'a' = weiße Form im Alphakanal, 'i' = Graustufe auf Schwarz.
VEKTOR = {
    'misc/crosshair_old': dict(m=m_old, kanal='a', p=dict(r=27.99, w=2.012, lw=1.0, tw=2.996),
                               mess=dict(mx=32.0, my=32.0, lx=31.5, ly=32.5)),
    'misc/crosshair_olde': dict(m=m_olde, kanal='a', p=dict(
        R=29.48, Rw=2.0, span=23.54, lw=1.031, hw=3.784, hI=0.5654, hs=1.179, cr=8.018, cw=1.207, cI=0.7648),
        mess=dict(mx=31.5, my=31.5)),
    'misc/crosshair_shaded': dict(m=m_shaded, kanal='a', p=dict(
        R=19.98, w=0.9966, span=44.19, ri=9.428, ra=15.54, si=1.091, sa=0.8422, I=0.3418),
        mess=dict(mx=31.5, my=31.5)),
    'misc/crosshair_triangle': dict(m=m_triangle, kanal='a', p=dict(
        r=13.84, aw=1.668, span=21.17, lw=1.371, r0=2.53), mess=dict(mx=16.0, my=16.0)),
    'misc/crosshair_circle': dict(m=m_circle, kanal='a', p=dict(
        rd=1.152, r1=12.6, w1=1.611, I1=0.7154, span1=21.47, r2=8.803, w2=1.368, I2=0.3091, span2=21.54,
        r3=6.276, w3=1.149, I3=0.1488, span3=20.14), mess=dict(mx=16.0, my=16.0)),
    'misc/crosshair_arrow': dict(m=m_arrow, kanal='i', p=dict(
        tx=8.698, ex=13.51, ey=4.92, aw=2.035, th=2.114, lh=7.436, tv=3.627, lv=7.321, iw=0.9566, iI=0.5287,
        pd=1.039, g=0.6984, gs=1.5), mess=dict(mx=16.0, my=16.0)),
    'misc/arc_bar': dict(m=m_arc_bar, kanal='a', p=dict(
        cx1=249.8, cy1=128.7, a1=215.9, b1=126.2, cx2=248.5, cy2=127.9, a2=187.0, b2=129.8)),
    'misc/arc_outline': dict(m=m_arc_outline, kanal='a', p=dict(
        cx1=249.4, cy1=128.1, a1=216.8, b1=126.7, cx2=247.5, cy2=127.6, a2=184.5, b2=127.9,
        w=2.766, f0=173.7, f1=234.5)),
}

# Schrittweiten für --anpassen (Rest: 0.25)
SCHRITT = {'span': 2.0, 'span1': 2.0, 'span2': 2.0, 'span3': 2.0, 'I': 0.05, 'I1': 0.05, 'I2': 0.05,
           'I3': 0.05, 'iI': 0.05, 'hI': 0.05, 'cI': 0.05, 'g': 0.05,
           'cx1': 2.0, 'cy1': 2.0, 'a1': 2.0, 'b1': 2.0, 'cx2': 2.0, 'cy2': 2.0, 'a2': 2.0, 'b2': 2.0,
           'f0': 4.0, 'f1': 4.0}


def vektor_kanal(ziel, w, h, S, p=None):
    """Deckung in Ausgabegröße. Die 'mess'-Werte (Lage im Original, z. B. ein
    um ein halbes Pixel versetzter Mittelpunkt) gelten nur beim Messen."""
    v = VEKTOR[ziel]
    return v['m'](Leinwand(w, h, S), dict(v['p'], **(p or {})))


def vektor_bild(ziel, orig, S=4):
    w, h = orig.size
    a = vektor_kanal(ziel, w, h, S)
    v8 = np.round(np.clip(a, 0, 1) * 255).astype(np.uint8)
    if VEKTOR[ziel]['kanal'] == 'i':
        rgba = np.dstack([v8, v8, v8, np.full_like(v8, 255)])
    else:
        rgba = np.dstack([np.full_like(v8, 255)] * 3 + [v8])
    return Image.fromarray(rgba, 'RGBA').convert(orig.mode)


def _vergleichskanal(ziel, orig):
    a = np.asarray(orig.convert('RGBA')).astype(np.float64) / 255
    return a[..., 0] if VEKTOR[ziel]['kanal'] == 'i' else a[..., 3]


def vektor_fehler(ziel, orig, p=None, S=4):
    """Mittlerer quadratischer Fehler: Modell auf Originalgröße verkleinert gegen Original."""
    w, h = orig.size
    p = dict(VEKTOR[ziel].get('mess', {}), **(p or {}))
    a = vektor_kanal(ziel, w, h, S, p).reshape(h, S, w, S).mean(axis=(1, 3))
    return float(np.mean((a - _vergleichskanal(ziel, orig)) ** 2))


def vektor_anpassen(ziel, orig, runden=40):
    """Koordinatensuche über alle Parameter; Ergebnis wird ausgegeben."""
    p = dict(VEKTOR[ziel]['p'], **VEKTOR[ziel].get('mess', {}))
    step = {k: SCHRITT.get(k, 0.25) for k in p}
    best = vektor_fehler(ziel, orig, p)
    for _ in range(runden):
        besser = False
        for k in p:
            for s in (1, -1):
                q = dict(p)
                q[k] = p[k] + s * step[k]
                e = vektor_fehler(ziel, orig, q)
                if e < best:
                    best, p, besser = e, q, True
                    break
            else:
                step[k] *= 0.5
        if not besser and max(step.values()) < 0.01:
            break
    return p, best

# ======================================================================
# Skalieren fotoartiger Texturen
# ======================================================================


def _farbe_ausbluten(rgb, a, schritte=8):
    """Farbe deckender Pixel in durchsichtige ausbreiten (keine dunklen Säume)."""
    rgb = rgb.copy()
    gueltig = a > 0.02
    for _ in range(schritte):
        if gueltig.all():
            break
        p = np.pad(rgb * gueltig[..., None], ((1, 1), (1, 1), (0, 0)), mode='edge')
        g = np.pad(gueltig.astype(np.float64), 1, mode='edge')
        summe = sum(p[1 + dy:p.shape[0] - 1 + dy, 1 + dx:p.shape[1] - 1 + dx]
                    for dy in (-1, 0, 1) for dx in (-1, 0, 1))
        anz = sum(g[1 + dy:g.shape[0] - 1 + dy, 1 + dx:g.shape[1] - 1 + dx]
                  for dy in (-1, 0, 1) for dx in (-1, 0, 1))
        neu = (~gueltig) & (anz > 0)
        rgb[neu] = summe[neu] / anz[neu][:, None]
        gueltig = gueltig | neu
    return rgb


def _lanczos(kanal, W, H):
    return np.asarray(Image.fromarray(kanal.astype(np.float32), 'F').resize((W, H), Image.LANCZOS), dtype=np.float64)


def skalieren(orig, f, kachel=False, alpha='weich', schaerfe=0.5):
    """Vergrößert um Faktor f. alpha: 'weich' (nur Lanczos) oder 'scharf'
    (Kante auf etwa 1,5 Ausgabepixel aufgesteilt, für Ausschnitte)."""
    hat_a = orig.mode == 'RGBA'
    arr = np.asarray(orig.convert('RGBA')).astype(np.float64) / 255
    rgb, a = arr[..., :3], arr[..., 3]
    if hat_a:
        rgb = _farbe_ausbluten(rgb, a)
    rand = 4
    modus = 'wrap' if kachel else 'edge'
    rgb = np.pad(rgb, ((rand, rand), (rand, rand), (0, 0)), mode=modus)
    a = np.pad(a, rand, mode=modus)
    h, w = a.shape
    W, H = w * f, h * f
    out = np.dstack([_lanczos(rgb[..., i], W, H) for i in range(3)])
    if schaerfe > 0:  # leichte Unschärfemaske
        out = np.dstack([c + schaerfe * (c - gauss(c, 0.6 * f)) for c in out.transpose(2, 0, 1)])
    if hat_a:
        a = _lanczos(a, W, H)
        if alpha == 'scharf':
            a = (a - 0.5) * (f / 1.5) + 0.5
        out = np.dstack([out, a])
    b = rand * f
    out = out[b:H - b, b:W - b]
    out = np.round(np.clip(out, 0, 1) * 255).astype(np.uint8)
    return Image.fromarray(out, 'RGBA' if hat_a else 'RGB')

# ======================================================================
# Ziele
# ======================================================================

FAKTOR = 4         # Vergrößerung, wenn nichts anderes gilt
MAX_SEITE = 1024   # längste Seite der Ausgabe
# Kleinere Obergrenzen: TS2 hält Texturen in festem Speicher. Mit 4fachen
# Bögen (1024x512) und 256er Fadenkreuzen brach der Levelstart mit exit(1)
# ab; Redux-Größen (Bögen 512x256, Fadenkreuze ~100) laufen.
SEITE = {'misc/crosshair_*': 128, 'misc/arc_*': 512, 'misc/ts2banner1b': 512}

# Skalieren: Pfad -> Optionen
SKALIEREN = {
    '2416': dict(kachel=True, schaerfe=0.8),  # Riffelblech
    '2464': dict(schaerfe=0.8),  # Kiste "THIS SIDE UP" (Seitenfläche, kachelt nicht)
    '2475': dict(alpha='scharf'),                 # verschneite Tanne (Ausschnitt)
    '2476': dict(kachel=True, alpha='scharf'),    # Maschendraht-Knoten (Ausschnitt)
    'misc/crosshair_laser': dict(schaerfe=0.3),  # weicher Schein, kein Vektor
    'misc/ts2banner1b': dict(),
    'misc/ts2loading': dict(),
}
for _n in range(1, 5):
    SKALIEREN[f'misc/ts2smallbanner{_n}'] = dict()


def alle_ziele(spiel):
    ziele = {z: ('vektor', None) for z in VEKTOR}
    for z, opt in SKALIEREN.items():
        ziele[z] = ('skalieren', opt)
    misc = os.path.join(spiel, TEX, 'misc')
    if os.path.isdir(misc):
        for n in sorted(os.listdir(misc)):
            if n.startswith('gmod_') and n.endswith('.rgb'):
                ziele['misc/' + n[:-4]] = ('skalieren', dict(alpha='scharf'))
    return ziele


def faktor_fuer(ziel, w, h):
    grenze = MAX_SEITE
    for m, seite in SEITE.items():
        if fnmatch.fnmatch(ziel, m):
            grenze = seite
    f = FAKTOR
    while f > 1 and max(w, h) * f > grenze:
        f //= 2
    return f


def sgi_speichern(im, pfad):
    """SGI unkomprimiert speichern, mit leerem Bildnamen im Kopf.

    Pillow schreibt den Dateinamen in das Namensfeld (Byte 24-103). TS2
    kopiert dieses Feld in einen kleinen Puffer: ab etwa 16 Zeichen
    (z. B. "gmod_monkey_assist") stürzt das Spiel beim Laden ab. Die
    Originale lassen das Feld leer."""
    im.save(pfad, format='SGI')
    with open(pfad, 'r+b') as f:
        f.seek(24)
        f.write(bytes(80))


def passt(ziel, muster):
    """Ziel ('misc/crosshair_old') passt auf eines der Muster (oder keine Muster)."""
    return not muster or any(fnmatch.fnmatch(ziel, m) or fnmatch.fnmatch(ziel.split('/')[-1], m) for m in muster)


def relpfad(ziel):
    return os.path.join(TEX, *ziel.split('/')) + '.rgb'


def erzeugen(spiel, muster):
    ziele = alle_ziele(spiel)
    n = 0
    for ziel, (art, opt) in sorted(ziele.items()):
        if not passt(ziel, muster):
            continue
        quelle = os.path.join(spiel, relpfad(ziel))
        if not os.path.exists(quelle):
            print(f'  fehlt: {quelle}')
            continue
        orig = Image.open(quelle)
        orig.load()
        f = faktor_fuer(ziel, *orig.size)
        if art == 'vektor':
            neu = vektor_bild(ziel, orig, f)
        else:
            neu = skalieren(orig, f, **opt)
        ziel_datei = os.path.join(spiel, 'hd_eigen', relpfad(ziel))
        os.makedirs(os.path.dirname(ziel_datei), exist_ok=True)
        sgi_speichern(neu, ziel_datei)
        print(f'  {ziel:28s} {art:9s} {orig.size[0]}x{orig.size[1]} {orig.mode} -> {neu.size[0]}x{neu.size[1]} {neu.mode}')
        n += 1
    print(f'{n} Texturen nach {os.path.join(spiel, "hd_eigen")}')


def eigene_dateien(spiel, muster=()):
    basis = os.path.join(spiel, 'hd_eigen')
    for wurzel, _, dateien in os.walk(basis):
        for d in sorted(dateien):
            if d.endswith('.rgb'):
                rel = os.path.relpath(os.path.join(wurzel, d), basis)
                ziel = os.path.relpath(rel, TEX)[:-4].replace(os.sep, '/')
                if passt(ziel, muster):
                    yield rel


def installieren(spiel, muster=()):
    """hd_eigen -> hd; ersetzte Dateien einmalig nach hd_backup sichern."""
    n = 0
    for rel in eigene_dateien(spiel, muster):
        q = os.path.join(spiel, 'hd_eigen', rel)
        z = os.path.join(spiel, 'hd', rel)
        b = os.path.join(spiel, 'hd_backup', rel)
        if os.path.exists(z) and not os.path.exists(b) and not filecmp.cmp(q, z, shallow=False):
            os.makedirs(os.path.dirname(b), exist_ok=True)
            shutil.copy2(z, b)
        os.makedirs(os.path.dirname(z), exist_ok=True)
        shutil.copy2(q, z)
        n += 1
    print(f'{n} eigene Texturen nach {os.path.join(spiel, "hd")} (Sicherung: hd_backup)')


def zuruecksetzen(spiel, muster=()):
    """Gesicherte Dateien zurück nach hd; eigene ohne Sicherung entfernen."""
    n = m = 0
    for rel in eigene_dateien(spiel, muster):
        q = os.path.join(spiel, 'hd_eigen', rel)
        z = os.path.join(spiel, 'hd', rel)
        b = os.path.join(spiel, 'hd_backup', rel)
        if os.path.exists(b):
            shutil.copy2(b, z)
            n += 1
        elif os.path.exists(z) and filecmp.cmp(q, z, shallow=False):
            os.remove(z)
            m += 1
    print(f'{n} Dateien aus hd_backup zurückgeschrieben, {m} eigene ohne Vorgänger entfernt')


def liste(spiel, muster):
    basis = os.path.join(spiel, TEX)
    n = 0
    for wurzel, _, dateien in os.walk(basis):
        for d in sorted(dateien):
            if not d.endswith('.rgb'):
                continue
            rel = os.path.relpath(os.path.join(wurzel, d), basis).replace(os.sep, '/')
            if muster and not any(fnmatch.fnmatch(rel, m) for m in muster):
                continue
            try:
                with Image.open(os.path.join(wurzel, d)) as im:
                    info = f'{im.size[0]:4d}x{im.size[1]:<4d} {im.mode:4s}'
            except Exception as e:  # noqa: BLE001 - Liste soll durchlaufen
                info = f'unlesbar ({e})'
            marken = [k for k, o in (('hd', 'hd'), ('eigen', 'hd_eigen'))
                      if os.path.exists(os.path.join(spiel, o, TEX, rel))]
            print(f'{rel:40s} {info} {" ".join(marken)}')
            n += 1
    print(f'{n} Texturen')

# ======================================================================
# Vergleichsbilder: Original | Redux | eigen
# ======================================================================

GRUPPEN = [('fadenkreuz', 'misc/crosshair_', 224), ('hud', 'misc/arc_', 512), ('level', '2', 256),
           ('symbole', 'misc/gmod_', 160), ('banner', 'misc/ts2', 512)]


def _zelle(im, gross, hinter):
    w, h = im.size
    s = gross / max(w, h)
    im = im.convert('RGBA').resize((max(1, round(w * s)), max(1, round(h * s))), Image.BILINEAR)
    bg = Image.new('RGBA', im.size, hinter)
    bg.alpha_composite(im)
    return bg.convert('RGB')


def vergleich(spiel, ordner):
    os.makedirs(ordner, exist_ok=True)
    ziele = sorted(alle_ziele(spiel))
    for name, praefix, gross in GRUPPEN:
        reihen = []
        for z in ziele:
            if not z.startswith(praefix) or z.startswith('misc/') and praefix == '2':
                continue
            rel = relpfad(z)
            eigen = os.path.join(spiel, 'hd_eigen', rel)
            redux = os.path.join(spiel, 'hd_backup', rel)
            if not os.path.exists(redux):
                redux = os.path.join(spiel, 'hd', rel)
                if os.path.exists(redux) and os.path.exists(eigen) and filecmp.cmp(redux, eigen, shallow=False):
                    redux = None
            spalten = []
            for p in (os.path.join(spiel, rel), redux, eigen):
                spalten.append(Image.open(p) if p and os.path.exists(p) else None)
            reihen.append((z, spalten))
        if not reihen:
            continue
        hinter = (40, 70, 45, 255)
        zellen = [[_zelle(im, gross, hinter) if im else None for im in sp] for _, sp in reihen]
        cw = max(c.width for r in zellen for c in r if c) + 8
        hs = [max(c.height for c in r if c) + 22 for r in zellen]
        blatt = Image.new('RGB', (cw * 3, sum(hs) + 20), (16, 16, 16))
        d = ImageDraw.Draw(blatt)
        for i, t in enumerate(('Original', 'Redux (game/hd)', 'eigen (game/hd_eigen)')):
            d.text((i * cw + 4, 4), t, fill=(255, 255, 160))
        y = 20
        for (z, sp), r, h in zip(reihen, zellen, hs):
            for i, (c, im) in enumerate(zip(r, sp)):
                if c:
                    blatt.paste(c, (i * cw + 4, y + 18))
                    d.text((i * cw + 4, y + 4), f'{z.split("/")[-1]} {im.size[0]}x{im.size[1]} {im.mode}', fill=(220, 220, 220))
                else:
                    d.text((i * cw + 4, y + 4), '-', fill=(220, 220, 220))
            y += h
        p = os.path.join(ordner, f'vergleich_{name}.png')
        blatt.save(p)
        print(p)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--spiel', default=os.path.join(REPO, 'game'), help='game-Ordner (Standard: <Repo>/game)')
    ap.add_argument('--nur', nargs='*', default=[], help='nur diese Ziele (Muster, z. B. "crosshair_*")')
    ap.add_argument('--liste', nargs='*', metavar='MUSTER', help='Originaltexturen mit Größe auflisten')
    ap.add_argument('--installieren', action='store_true', help='hd_eigen nach hd kopieren (Sicherung hd_backup)')
    ap.add_argument('--zuruecksetzen', action='store_true', help='hd aus hd_backup wiederherstellen')
    ap.add_argument('--vergleich', metavar='ORDNER', help='Vergleichsbilder Original|Redux|eigen schreiben')
    ap.add_argument('--messen', action='store_true', help='Abweichung der Vektor-Modelle vom Original')
    ap.add_argument('--anpassen', action='store_true', help='Vektor-Parameter ans Original anpassen und ausgeben')
    args = ap.parse_args()
    spiel = os.path.abspath(args.spiel)
    if not os.path.isdir(os.path.join(spiel, TEX)):
        sys.exit(f'keine Originaltexturen unter {os.path.join(spiel, TEX)}')
    if args.liste is not None:
        return liste(spiel, args.liste)
    if args.messen or args.anpassen:
        for ziel in VEKTOR:
            if not passt(ziel, args.nur):
                continue
            orig = Image.open(os.path.join(spiel, relpfad(ziel)))
            e = vektor_fehler(ziel, orig)
            if args.anpassen:
                p, e2 = vektor_anpassen(ziel, orig)
                print(f'{ziel}: RMS {np.sqrt(e) * 255:.1f} -> {np.sqrt(e2) * 255:.1f}')
                mess = VEKTOR[ziel].get('mess', {})
                print('   p=dict(' + ', '.join(f'{k}={v:.4g}' for k, v in p.items() if k not in mess) + ')')
                if mess:
                    print('   mess=dict(' + ', '.join(f'{k}={p[k]:.4g}' for k in mess) + ')')
            else:
                print(f'{ziel}: RMS-Abweichung {np.sqrt(e) * 255:.1f} (von 255)')
        return
    if args.zuruecksetzen:
        return zuruecksetzen(spiel, args.nur)
    if args.vergleich:
        return vergleich(spiel, args.vergleich)
    if args.installieren:
        return installieren(spiel, args.nur)
    erzeugen(spiel, args.nur)


if __name__ == '__main__':
    main()
