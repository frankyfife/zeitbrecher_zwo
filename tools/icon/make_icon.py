"""Zeichnet das Programmsymbol von Zeitbrecher Zwo (eigene Grafik, keine
Vorlage aus dem Spiel): ein Zifferblatt, das entlang eines Zickzack-Risses in
zwei versetzte Hälften zerbrochen ist (die Zeit "bricht"), links bernstein-,
rechts türkisfarben (zwei Hälften: "Zwo"), im Riss ein helles Leuchten.

Aufruf: python -I tools/icon/make_icon.py [Vorschau-Ordner]

Schreibt src/launcher/res/zeitbrecher.ico (16, 20, 24, 32, 40, 48, 64, 128,
256 px; 256 als PNG, die übrigen als 32-Bit-BMP) und die Vorschau
zeitbrecher_vorschau.png (alle Größen auf hellem und dunklem Grund) in den
Vorschau-Ordner (Vorgabe: src/launcher/res).

Jede Größe wird eigens gezeichnet (kleine Größen mit dickeren Linien, ohne
Striche und Zacken), in 1024er Auflösung berechnet und dann gemittelt.
"""
import io
import math
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageFilter

SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ICO = os.path.join(ROOT, 'src', 'launcher', 'res', 'zeitbrecher.ico')


def hexrgb(h):
    h = h.lstrip('#')
    return np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)])


# Farben
FACE_IN = hexrgb('#2a3560')    # Zifferblatt innen
FACE_OUT = hexrgb('#111830')   # Zifferblatt am Rand
WARM_A = hexrgb('#ffd04a')     # linke Hälfte: Bernstein
WARM_B = hexrgb('#ff7a1c')
COOL_A = hexrgb('#5cf0ff')     # rechte Hälfte: Türkis
COOL_B = hexrgb('#1f8bff')
HAND = hexrgb('#f4f6ff')       # Zeiger, Striche
GLOW_CORE = hexrgb('#ffffff')
GLOW_HALO = hexrgb('#c46bff')  # Violett: das Leuchten im Riss
OUTLINE = hexrgb('#0a0f22')


def params(size):
    """Maße in Bruchteilen der Kantenlänge, nach Größe abgestuft."""
    if size <= 24:
        return dict(R=0.43, rim=0.14, gap=0.07, slide=0.0, zig=0.0, ticks=0, hand_h=0.13, hand_m=0.11,
                    len_h=0.14, len_m=0.2, hub=0.0, outline=0.0, glow=0.07, core=0.02, shadow=False)
    if size <= 48:
        return dict(R=0.42, rim=0.095, gap=0.05, slide=0.01, zig=0.045, ticks=4, hand_h=0.085, hand_m=0.07,
                    len_h=0.16, len_m=0.24, hub=0.05, outline=0.012, glow=0.055, core=0.013, shadow=False)
    return dict(R=0.40, rim=0.07, gap=0.035, slide=0.015, zig=0.06, ticks=12, hand_h=0.06, hand_m=0.045,
                len_h=0.16, len_m=0.245, hub=0.04, outline=0.01, glow=0.045, core=0.007, shadow=True)


# Riss: steigt von links unten nach rechts oben (60 Grad); n zeigt nach rechts unten
ANG = math.radians(60)
TV = np.array([math.cos(ANG), -math.sin(ANG)])     # entlang des Risses (Bild-y nach unten)
NV = np.array([math.sin(ANG), math.cos(ANG)])      # quer dazu, zur rechten Hälfte
C = np.array([0.5, 0.5])


# Riss als Zickzack wie ein Blitz: Stützpunkte entlang (t) und quer (s), in Radien
CRACK_T = (-1.4, -0.78, -0.42, -0.02, 0.36, 0.74, 1.4)
CRACK_S = (0.0, 0.75, -0.55, 0.6, -0.65, 0.5, 0.0)


def crack_offset(t, p):
    if p['zig'] <= 0:
        return np.zeros_like(t)
    R = p['R']
    return p['zig'] * np.interp(t, [x * R for x in CRACK_T], CRACK_S)


def smooth(edge0, edge1, x):
    k = np.clip((x - edge0) / (edge1 - edge0), 0, 1)
    return k * k * (3 - 2 * k)


def seg_dist(px, py, ax, ay, bx, by):
    vx, vy = bx - ax, by - ay
    h = np.clip(((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy), 0, 1)
    return np.hypot(px - ax - h * vx, py - ay - h * vy)


def over(dst, rgb, a):
    """Legt Farbe rgb (3,) oder (...,3) mit Deckung a über dst (vormultipliziert)."""
    a = a[..., None]
    col = rgb * a
    dst[..., :3] = col + dst[..., :3] * (1 - a)
    dst[..., 3:4] = a + dst[..., 3:4] * (1 - a)


def clock(qx, qy, p, warm, aa):
    """Zifferblatt an den (verschobenen) Punkten q: Farbe (vormultipliziert)."""
    out = np.zeros(qx.shape + (4,))
    dx, dy = qx - C[0], qy - C[1]
    r = np.hypot(dx, dy)
    R = p['R']
    cov = lambda d: smooth(aa, -aa, d)   # Abdeckung aus vorzeichenbehaftetem Abstand
    # dunkler Rand außen (heller Hintergrund)
    if p['outline'] > 0:
        over(out, OUTLINE, cov(r - (R + p['outline'])) * 0.9)
    # Zifferblatt: radialer Verlauf, oben etwas heller
    k = np.clip(r / R, 0, 1)[..., None]
    face = FACE_IN * (1 - k) + FACE_OUT * k
    face = face + 0.06 * np.clip(-dy / R, 0, 1)[..., None]
    over(out, face, cov(r - R))
    # Striche
    ring_in = R - p['rim']
    if p['ticks']:
        for i in range(p['ticks']):
            a = 2 * math.pi * i / p['ticks']
            major = (i * 12 // p['ticks']) % 3 == 0
            ln = (0.07 if major else 0.04) if p['ticks'] == 12 else 0.06
            w = (0.022 if major else 0.012) if p['ticks'] == 12 else 0.03
            ux, uy = math.sin(a), -math.cos(a)
            r0, r1 = ring_in - 0.015 - ln, ring_in - 0.015
            d = seg_dist(qx, qy, C[0] + ux * r0, C[1] + uy * r0, C[0] + ux * r1, C[1] + uy * r1) - w / 2
            over(out, HAND * 0.92, cov(d))
    # Zeiger: 10 nach 10
    for ang, ln, w in ((-60, p['len_h'], p['hand_h']), (60, p['len_m'], p['hand_m'])):
        a = math.radians(ang)
        ex, ey = C[0] + math.sin(a) * ln, C[1] - math.cos(a) * ln
        d = seg_dist(qx, qy, C[0], C[1], ex, ey) - w / 2
        over(out, HAND, cov(d))
    if p['hub'] > 0:
        over(out, warm[0] if warm is not None else HAND, cov(r - p['hub']))
    # Rand: Farbverlauf über den Winkel
    ang = np.arctan2(dy, dx)
    g = ((np.sin(ang - ANG) + 1) / 2)[..., None]
    ring = warm[0] * (1 - g) + warm[1] * g
    ring_cov = cov(r - R) * cov(ring_in - r)
    over(out, ring, ring_cov)
    # feiner Glanz auf dem Rand oben
    over(out, np.array([1.0, 1.0, 1.0]), ring_cov * 0.25 * np.clip(-dy / R, 0, 1) ** 2)
    return out


def render(size):
    p = params(size)
    ss = int(math.ceil(1024 / size))
    n = size * ss
    aa = 0.75 / n
    u = (np.arange(n) + 0.5) / n
    px, py = np.meshgrid(u, u)
    img = np.zeros((n, n, 4))

    # Leuchten im Riss (unter den Hälften: es scheint durch den Spalt)
    rel_x, rel_y = px - C[0], py - C[1]
    t = rel_x * TV[0] + rel_y * TV[1]
    s = rel_x * NV[0] + rel_y * NV[1]
    d = np.abs(s - crack_offset(t, p))
    r = np.hypot(rel_x, rel_y)
    inside = smooth(p['R'] * 1.12, p['R'] * 0.85, r)
    halo = np.exp(-(d / p['glow']) ** 2) * inside
    over(img, GLOW_HALO, np.clip(halo * 1.15, 0, 1))
    core = smooth(p['core'] + aa, p['core'] - aa, d) * smooth(p['R'] * 1.02, p['R'] * 0.9, r)
    over(img, GLOW_CORE, core)

    # zwei Hälften, auseinandergeschoben
    for sign, cols in ((-1, (WARM_A, WARM_B)), (1, (COOL_A, COOL_B))):
        # auseinander (quer zum Riss) und gegeneinander verrutscht (entlang)
        g, sl = p['gap'], p['slide']
        qx = px - sign * (g * NV[0] - sl * TV[0])
        qy = py - sign * (g * NV[1] - sl * TV[1])
        lx, ly = qx - C[0], qy - C[1]
        tq = lx * TV[0] + ly * TV[1]
        sq = lx * NV[0] + ly * NV[1] - crack_offset(tq, p)
        half = smooth(-aa, aa, sign * sq)
        layer = clock(qx, qy, p, cols, aa)
        layer *= half[..., None]
        a = layer[..., 3:4]
        img[..., :3] = layer[..., :3] + img[..., :3] * (1 - a)
        img[..., 3:4] = a + img[..., 3:4] * (1 - a)

    # mitteln (vormultipliziert)
    img = img.reshape(size, ss, size, ss, 4).mean(axis=(1, 3))
    a = img[..., 3:4]
    rgb = np.where(a > 1e-6, img[..., :3] / np.maximum(a, 1e-6), 0)
    out = np.concatenate([np.clip(rgb, 0, 1), np.clip(a, 0, 1)], axis=-1)
    im = Image.fromarray((out * 255 + 0.5).astype(np.uint8), 'RGBA')

    if p['shadow']:
        # weicher Schatten nach unten wie bei Windows-11-Symbolen
        sh = Image.new('RGBA', im.size, (0, 0, 0, 0))
        alpha = im.getchannel('A').point(lambda v: v * 0.35)
        sh.putalpha(alpha)
        sh = sh.filter(ImageFilter.GaussianBlur(size * 0.02))
        base = Image.new('RGBA', im.size, (0, 0, 0, 0))
        base.alpha_composite(sh, (0, max(1, size // 64)))
        base.alpha_composite(im)
        im = base
    return im


def bmp_entry(im):
    """32-Bit-DIB für ICO: BITMAPINFOHEADER (doppelte Höhe), BGRA von unten, AND-Maske."""
    w, h = im.size
    px = np.array(im.convert('RGBA'))
    bgra = px[::-1, :, [2, 1, 0, 3]].tobytes()
    row = ((w + 31) // 32) * 4
    mask = bytearray()
    for y in range(h - 1, -1, -1):
        bits = 0
        line = bytearray(row)
        for x in range(w):
            if px[y, x, 3] == 0:
                line[x // 8] |= 0x80 >> (x % 8)
        mask += line
    hdr = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 32, 0, len(bgra) + len(mask), 0, 0, 0, 0)
    return hdr + bgra + bytes(mask)


def write_ico(images, path):
    entries = []
    for im in images:
        if im.size[0] >= 256:
            buf = io.BytesIO()
            im.save(buf, 'PNG', optimize=True)
            entries.append((im.size[0], buf.getvalue()))
        else:
            entries.append((im.size[0], bmp_entry(im)))
    data = struct.pack('<HHH', 0, 1, len(entries))
    off = 6 + 16 * len(entries)
    blobs = b''
    for s, blob in entries:
        data += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(blob), off + len(blobs))
        blobs += blob
    with open(path, 'wb') as f:
        f.write(data + blobs)


def preview(images, path):
    """Alle Größen in echter Größe und 256er Ansicht vergrößert, hell und dunkel."""
    pad = 12
    w = sum(im.size[0] for im in images) + pad * (len(images) + 1)
    w = max(w, 2 * 8 * (16 + 24 + 32) + 7 * pad)
    h = 256 + 2 * pad
    sheet = Image.new('RGBA', (w, h * 2 + 8 * 32 + 2 * pad), (0, 0, 0, 255))
    for row, bg in enumerate(((243, 243, 243, 255), (32, 32, 32, 255))):
        band = Image.new('RGBA', (w, h), bg)
        x = pad
        for im in images:
            band.alpha_composite(im, (x, (h - im.size[1]) // 2))
            x += im.size[0] + pad
        sheet.paste(band, (0, row * h))
    # 16, 24, 32 achtfach vergrößert (Pixel sichtbar)
    y = 2 * h + pad
    x = pad
    for im in images:
        if im.size[0] in (16, 24, 32):
            for bg in ((243, 243, 243, 255), (32, 32, 32, 255)):
                tile = Image.new('RGBA', im.size, bg)
                tile.alpha_composite(im)
                big = tile.resize((im.size[0] * 8, im.size[1] * 8), Image.NEAREST)
                sheet.paste(big, (x, y))
                x += big.size[0] + pad
    sheet.save(path)


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(ICO)
    images = [render(s) for s in SIZES]
    os.makedirs(os.path.dirname(ICO), exist_ok=True)
    write_ico(images, ICO)
    os.makedirs(out_dir, exist_ok=True)
    preview(images, os.path.join(out_dir, 'zeitbrecher_vorschau.png'))
    images[-1].save(os.path.join(out_dir, 'zeitbrecher_256.png'))
    print('geschrieben:', ICO)


if __name__ == '__main__':
    main()
