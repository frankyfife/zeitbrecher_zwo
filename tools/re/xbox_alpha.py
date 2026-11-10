"""Vergleicht den Alphakanal der PC-Texturen mit den Xbox-Texturen.

Aufruf: python -I tools/re/xbox_alpha.py <xbox.iso> <game-Ordner> [--tsv bericht.tsv] [--bin game/xbox_alpha.bin]

Liest alle Paks (data/**/*.pak) aus dem eigenen Xbox-Image (nur lesend, wie
xiso.py), dekodiert von jeder textures/**/*.xbt die oberste Stufe und
vergleicht ihr Alpha mit game/ts2/textures/**/*.rgb (SGI) und, falls
vorhanden, der Fassung in game/hd/ts2/textures (TS2-Redux-Texturen, die
ts2.exe zuerst liest). Ergebnis: eine Zeile je Textur mit wesentlichem
Unterschied, nach Art sortiert (--tsv: zusätzlich als Tabelle).

--bin schreibt für ts2.exe Ersatzinhalte der Texturen mit wesentlichem
Unterschied (deckend auf dem PC, anders, umgekehrt; gleiches Bild): Alpha
der Xbox, Farbe der PC-Datei, bei Leuchttexturen (PC-Farbe = Xbox-Farbe mal
Alpha, Alpha verloren) die Xbox-Farbe; in Lage und Größe der geladenen
PC-Datei, zugeordnet über den Inhalts-Hash
(src/host/alphafix.cpp). Die Datei enthält Daten aus dem eigenen Image und
gehört nach game/ (nie ins Repo).

.xbt-Kopf (Little Endian): Breite, Höhe, Breite2, Höhe2, Stufen-1 (0 = keine
Stufen), Art; Daten ab 0x80, oberste Stufe zuerst, Zeilen von oben.
Art 0 = DXT1, 1 = DXT3, 2 = A8R8G8B8 (Xbox-swizzled). Pillow liest SGI mit
der untersten Dateizeile zuerst; die Xbox-Zeile 0 entspricht der letzten
Pillow-Zeile (und der ersten Zeile, die ts2.exe hochlädt).
"""
import os
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xiso  # noqa: E402


def pak_entries(data):
    """P8CK-Pak: Name -> Bytes"""
    if data[:4] != b'P8CK':
        return {}
    diro, n, nameo, namesz = struct.unpack_from('<4I', data, 4)
    names = data[nameo:nameo + namesz]
    out = {}
    for i in range(n):
        no, sz, off = struct.unpack_from('<3I', data, diro + i * 12)
        nm = names[no:names.index(b'\0', no)].decode('latin-1').lower()
        out[nm] = data[off:off + sz]
    return out


def morton(w, h):
    """Index im Xbox-swizzled Speicher für jedes (y, x)"""
    y, x = np.mgrid[0:h, 0:w]
    out = np.zeros((h, w), np.int64)
    bit = 0
    mw, mh, sx, sy = w, h, 0, 0
    while mw > 1 or mh > 1:
        if mw > 1:
            out |= ((x >> sx) & 1) << bit
            bit += 1
            sx += 1
            mw >>= 1
        if mh > 1:
            out |= ((y >> sy) & 1) << bit
            bit += 1
            sy += 1
            mh >>= 1
    return out


def blocks_to_image(vals, bw, bh, w, h):
    """(bh*bw, 16) Werte je Block -> (h, w)"""
    img = vals.reshape(bh, bw, 4, 4).transpose(0, 2, 1, 3).reshape(bh * 4, bw * 4)
    return img[:h, :w]


def color_blocks(blk, four):
    """DXT-Farbteil (n, 8) -> RGB (n, 16, 3) und Alpha (n, 16); four: immer 4 Farben (DXT3)"""
    c0 = blk[:, 0].astype(np.int32) | (blk[:, 1].astype(np.int32) << 8)
    c1 = blk[:, 2].astype(np.int32) | (blk[:, 3].astype(np.int32) << 8)

    def rgb(c):
        r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
        return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], 1)

    p0, p1 = rgb(c0), rgb(c1)
    three = (c0 <= c1)[:, None] & (not four)
    p2 = np.where(three, (p0 + p1) // 2, (2 * p0 + p1) // 3)
    p3 = np.where(three, 0, (p0 + 2 * p1) // 3)
    pal = np.stack([p0, p1, p2, p3], 1)  # (n, 4, 3)
    idx = blk[:, 4:8].copy().view('<u4').ravel()
    sel = (idx[:, None] >> (2 * np.arange(16))) & 3
    col = np.take_along_axis(pal, sel[:, :, None].repeat(3, 2).astype(np.int64), 1)
    a = np.where(three & (sel == 3), 0, 255)
    return col.astype(np.uint8), a.astype(np.uint8)


def xbt_decode(b):
    """(w, h, art, RGBA (h, w, 4) uint8) der obersten Stufe, Zeilen von oben"""
    w, h, _, _, _, kind = struct.unpack_from('<6I', b, 0)
    bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
    if kind in (0, 1):
        bs = 8 if kind == 0 else 16
        blk = np.frombuffer(b, np.uint8, bw * bh * bs, 0x80).reshape(-1, bs)
        col, a = color_blocks(blk[:, bs - 8:], kind == 1)
        if kind == 1:  # DXT3: 4 Bit Alpha je Pixel
            v = blk[:, :8].copy().view('<u8').ravel()
            a = (((v[:, None] >> (4 * np.arange(16, dtype=np.uint64))) & 15) * 17).astype(np.uint8)
        out = np.zeros((h, w, 4), np.uint8)
        for c in range(3):
            out[..., c] = blocks_to_image(col[:, :, c], bw, bh, w, h)
        out[..., 3] = blocks_to_image(a, bw, bh, w, h)
        return w, h, kind, out
    if kind == 2:  # A8R8G8B8 (BGRA im Speicher), swizzled
        px = np.frombuffer(b, np.uint8, w * h * 4, 0x80).reshape(-1, 4)[morton(w, h).ravel()].reshape(h, w, 4)
        return w, h, kind, px[..., [2, 1, 0, 3]].copy()
    raise ValueError('unbekannte Art %d' % kind)


def pc_rgba(path):
    """RGBA (h, w, 4) in Upload-Zeilenfolge (= Xbox-Zeilenfolge), Modus"""
    im = Image.open(path)
    return np.ascontiguousarray(np.asarray(im.convert('RGBA'))[::-1]), im.mode


def fnv(a):
    """Inhalts-Hash wie ts2.exe (FNV-1a über die oberste Stufe, Upload-Zeilenfolge)"""
    h = 2166136261
    for x in a.tobytes():
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


def resize_alpha(a, w, h):
    if a.shape == (h, w):
        return a.astype(int)
    return np.asarray(Image.fromarray(a).resize((w, h), Image.BILINEAR)).astype(int)


ORIENT = [lambda a: a, lambda a: a[::-1], lambda a: a[:, ::-1], lambda a: a[::-1, ::-1],
          lambda a: a.swapaxes(0, 1), lambda a: a.swapaxes(0, 1)[::-1], lambda a: a.swapaxes(0, 1)[:, ::-1],
          lambda a: a.swapaxes(0, 1)[::-1, ::-1]]


def resize(a, w, h):
    """(h0, w0[, c]) uint8 -> (h, w[, c]) int"""
    a = np.ascontiguousarray(a)
    if a.shape[:2] == (h, w):
        return a.astype(int)
    return np.asarray(Image.fromarray(a).resize((w, h), Image.BILINEAR)).astype(int)


def premultiplied(x):
    """Xbox-Farbe mal Xbox-Alpha (so liegen die Leuchttexturen im PC-Port vor)"""
    return x[..., :3].astype(int) * x[..., 3:4].astype(int) // 255


def orient(x, p):
    """Xbox-RGBA x so drehen/spiegeln und skalieren, dass es am besten zur
    PC-Textur p passt (manche PC-Texturen liegen gedreht vor, z. B. 64x128
    statt 128x64). Verglichen wird die Farbe, für Leuchttexturen die mit dem
    Alpha multiplizierte Xbox-Farbe; das PC-Alpha entscheidet mit. Ergebnis:
    (Farbabstand, x in Größe und Lage von p, PC-Farbe ist vormultipliziert)"""
    ph, pw = p.shape[:2]
    best = None
    for f in ORIENT:
        c = f(x)
        if c.shape[0] * pw != c.shape[1] * ph:  # Seitenverhältnis muss passen
            continue
        r = resize(c, pw, ph)
        d_plain = np.abs(r[..., :3] - p[..., :3].astype(int)).mean()
        d_pre = np.abs(premultiplied(r) - p[..., :3].astype(int)).mean()
        d = min(d_plain, d_pre)
        # PC-Alpha entscheidet mit: voll, wenn es durchsichtige Stellen hat
        wa = 1.0 if (p[..., 3] < 128).mean() >= 0.01 else 0.05
        key = d + wa * np.abs(r[..., 3] - p[..., 3].astype(int)).mean()
        if best is None or key < best[0]:
            best = (key, d, r, d_pre < d_plain)
    return best[1:] if best else None


def classify(x, p):
    """Art des Unterschieds (oder None), Abweichung, Anteile durchsichtig, Xbox-Alpha in PC-Lage"""
    o = orient(x, p)
    if o is None:
        return None
    xa, pa = o[1][..., 3], p[..., 3].astype(int)
    x_trans, p_trans = (xa < 128).mean(), (pa < 128).mean()
    diff = np.abs(xa - pa).mean()
    art = None
    if pa.min() == 255 and x_trans >= 0.01:
        art = 'deckend'
    elif diff >= 16:
        if np.abs(xa - (255 - pa)).mean() < 8:
            art = 'umgekehrt'
        elif xa.min() == 255:
            art = 'pc-durchsichtig'
        else:
            art = 'anders'
    return (art, diff, x_trans, p_trans, o) if art else None


def main():
    args = sys.argv[1:]
    opt = {}
    for k in ('--tsv', '--bin'):
        if k in args:
            i = args.index(k)
            opt[k] = args[i + 1]
            del args[i:i + 2]
    iso, game = args[0], args[1]
    f, base = xiso.open_image(iso)
    f.seek(base + 32 * xiso.SECTOR + 20)
    root_sec, root_size = struct.unpack('<II', f.read(8))
    paks = [(p, sec, sz) for p, sec, sz in xiso.walk_tree(f, base, root_sec, root_size)
            if p.lower().startswith('data/') and p.lower().endswith('.pak')]
    tex = {}   # Name -> Bytes (erstes Vorkommen)
    where = {}  # Name -> Paks
    for p, sec, sz in sorted(paks):
        f.seek(base + sec * xiso.SECTOR)
        ents = pak_entries(f.read(sz))
        pk = os.path.basename(p).lower()[:-4]
        for nm, b in ents.items():
            if nm.startswith('textures/') and nm.endswith('.xbt'):
                tex.setdefault(nm, b)
                where.setdefault(nm, []).append(pk)
    rows, fixes, missing, stats = [], [], 0, {}
    for nm in sorted(tex):
        rel = nm[len('textures/'):-4] + '.rgb'
        try:
            w, h, kind, x = xbt_decode(tex[nm])
        except Exception as e:  # noqa: BLE001
            print('nicht lesbar:', nm, e, file=sys.stderr)
            continue
        stats[kind] = stats.get(kind, 0) + 1
        hd = os.path.join(game, 'hd', 'ts2', 'textures', rel)
        for tag, path in (('pc', os.path.join(game, 'ts2', 'textures', rel)), ('hd', hd)):
            if not os.path.exists(path):
                missing += tag == 'pc'
                continue
            try:
                p, mode = pc_rgba(path)
            except Exception:  # noqa: BLE001
                continue
            c = classify(x, p)
            if not c:
                continue
            black = int(p[..., :3].max()) <= 8
            rows.append((c[0], rel, tag, '%dx%d' % (w, h), '%dx%d' % (p.shape[1], p.shape[0]),
                         ('DXT1', 'DXT3', 'ARGB')[kind], mode, 'schwarz' if black else '',
                         '%.0f' % c[1], '%.0f%%' % (100 * c[2]), '%.0f%%' % (100 * c[3]),
                         ','.join(sorted(set(where[nm])))[:120]))
            # Korrektur für ts2.exe: nur die Datei, die es wirklich lädt (hd zuerst),
            # nur wesentliche Unterschiede bei passender Farbe (gleiches Bild)
            loaded = tag == 'hd' or not os.path.exists(hd)
            dist, xo, pre = c[4]
            if (loaded and c[0] in ('deckend', 'anders', 'umgekehrt') and c[1] >= 32 and dist <= 24
                    and fnv(p) not in {e[0] for e in fixes}):  # gleicher Inhalt nur einmal
                # vormultiplizierte Leuchttextur: Farbe wie auf der Xbox, sonst die PC-Farbe
                fix = np.empty_like(p)
                fix[..., :3] = xo[..., :3] if pre else p[..., :3]
                fix[..., 3] = xo[..., 3]
                fixes.append((fnv(p), p.shape[1], p.shape[0], fix, rel + (' (Farbe Xbox)' if pre else '')))
    order = {'deckend': 0, 'umgekehrt': 1, 'anders': 2, 'pc-durchsichtig': 3}
    rows.sort(key=lambda r: (order[r[0]], r[1], r[2]))
    print('Xbox-Texturen: %d (DXT1 %d, DXT3 %d, ARGB %d); ohne .rgb: %d; Unterschiede: %d; Korrekturen: %d' % (
        len(tex), stats.get(0, 0), stats.get(1, 0), stats.get(2, 0), missing, len(rows), len(fixes)))
    head = ('art', 'textur', 'datei', 'xbox', 'pc', 'format', 'modus', 'farbe', 'abw', 'xbox_durchs', 'pc_durchs', 'paks')
    for r in rows:
        print('\t'.join(r))
    for hsh, pw, ph, _, rel in fixes:
        print('Korrektur %08X %dx%d %s' % (hsh, pw, ph, rel))
    if '--tsv' in opt:
        with open(opt['--tsv'], 'w', encoding='utf-8') as o:
            o.write('\t'.join(head) + '\n')
            for r in rows:
                o.write('\t'.join(r) + '\n')
    if '--bin' in opt:
        # game/xbox_alpha.bin (ts2.exe, alphafix.cpp): "TS2A", Anzahl, je Eintrag
        # Hash, Breite, Höhe (je u32) und Breite·Höhe·4 Byte RGBA in Upload-Zeilenfolge
        with open(opt['--bin'], 'wb') as o:
            o.write(b'TS2A' + struct.pack('<I', len(fixes)))
            for hsh, pw, ph, a, _ in fixes:
                o.write(struct.pack('<3I', hsh, pw, ph) + np.ascontiguousarray(a, np.uint8).tobytes())


if __name__ == '__main__':
    main()
