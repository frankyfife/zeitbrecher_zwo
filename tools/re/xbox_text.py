"""Holt die Texte aller Sprachen aus dem eigenen Xbox-Image von TimeSplitters 2.

Aufruf: python -I xbox_text.py <image.iso> <game-Ordner>

Die PAL-Fassung (En, Fr, De, Es, It) hat in default.xbe fünf Texttabellen mit
je 2770 Zeigern (32 Bit), in derselben Reihenfolge wie die eine (englische)
Tabelle des PC-Ports (0x1424E2430): Eintrag 422 "Arcade", 425 "Story",
430 "Challenge", 2142 "Quit". Gefunden werden die Tabellen über den Zeiger
auf "Arcade" an Eintrag 422 (alle Sprachen teilen diesen Text), ihre Sprache
über Eintrag 2142.

Ergebnis: <game>/sprache/<code>.bin, je 2770 nullterminierte Texte
(Latin-1, wie in der XBE). Diese Dateien stammen aus dem eigenen Image und
gehören nie ins Repository.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xiso

ENTRIES = 2770
QUIT = {b'Quit': 'en', b'Quitter': 'fr', b'Beenden': 'de', b'Esci': 'it', b'Salir': 'es'}


def main():
    f, base = xiso.open_image(sys.argv[1])
    f.seek(base + 32 * xiso.SECTOR + 20)
    root_sec, root_size = struct.unpack('<II', f.read(8))
    xbe = None
    for p, sec, sz in xiso.walk_tree(f, base, root_sec, root_size):
        if p.lower() == 'default.xbe':
            f.seek(base + sec * xiso.SECTOR)
            xbe = f.read(sz)
    if xbe is None:
        raise SystemExit('default.xbe nicht im Image')
    xbase = struct.unpack_from('<I', xbe, 0x104)[0]
    nsec = struct.unpack_from('<I', xbe, 0x11C)[0]
    sh = struct.unpack_from('<I', xbe, 0x120)[0] - xbase
    secs = [struct.unpack_from('<IIIII', xbe, sh + i * 56)[1:] for i in range(nsec)]

    def va2off(va):
        for v, vs, ra, rs in secs:
            if v <= va < v + rs:
                return ra + va - v
        return None

    def off2va(o):
        for v, vs, ra, rs in secs:
            if ra <= o < ra + rs:
                return v + o - ra
        return None

    def text(va):
        o = va2off(va)
        return xbe[o:xbe.index(b'\0', o)] if o is not None and va else b''

    out_dir = os.path.join(sys.argv[2], 'sprache')
    os.makedirs(out_dir, exist_ok=True)
    found = {}
    for m in re.finditer(re.escape(b'Arcade\0'), xbe):
        arcade_va = off2va(m.start())
        if arcade_va is None:
            continue
        for p in re.finditer(re.escape(struct.pack('<I', arcade_va)), xbe):
            loc = off2va(p.start())
            if loc is None:
                continue
            table = loc - 422 * 4
            if va2off(table) is None or va2off(table + ENTRIES * 4 - 4) is None:
                continue
            entry = lambda i: struct.unpack_from('<I', xbe, va2off(table + i * 4))[0]
            lang = QUIT.get(text(entry(2142)))
            if not lang or lang in found or text(entry(425)) == b'':
                continue
            found[lang] = table
            with open(os.path.join(out_dir, lang + '.bin'), 'wb') as o:
                for i in range(ENTRIES):
                    o.write(text(entry(i)) + b'\0')
    for lang, table in sorted(found.items()):
        print(f'{lang}: Tabelle 0x{table:X} -> sprache/{lang}.bin')
    if not found:
        raise SystemExit('keine Texttabellen gefunden')


if __name__ == '__main__':
    main()
