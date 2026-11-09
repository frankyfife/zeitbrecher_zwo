"""Liest Dateien aus einem Xbox-Image (XISO, auch Redump mit Videopartition).

Aufruf: python -I xiso.py <image.iso> list
        python -I xiso.py <image.iso> get <pfad/in/image> <ziel>

Nur lesend. Aufbau: Kopf "MICROSOFT*XBOX*MEDIA" in Sektor 32 der
Spielpartition (bei Redump-Abbildern ab 0x18300000), darin Sektor und Größe
des Wurzelverzeichnisses; Verzeichnisse sind binäre Bäume aus Einträgen
(links/rechts in 4-Byte-Schritten, Sektor, Größe, Attribute, Name).
"""
import struct
import sys

SECTOR = 2048
MAGIC = b'MICROSOFT*XBOX*MEDIA'


def open_image(path):
    f = open(path, 'rb')
    for base in (0, 0x18300000, 0x0FD90000, 0x89D80000, 0x2080000):
        f.seek(base + 32 * SECTOR)
        if f.read(20) == MAGIC:
            return f, base
    raise SystemExit('kein XISO-Kopf gefunden')


def read_dir(f, base, sector, size):
    f.seek(base + sector * SECTOR)
    data = f.read(size)
    out = []

    def walk(off):
        if off * 4 + 14 > len(data):
            return
        left, right, sec, sz, attr, nlen = struct.unpack_from('<HHIIBB', data, off * 4)
        if left == 0xFFFF:
            return
        name = data[off * 4 + 14:off * 4 + 14 + nlen].decode('latin-1')
        out.append((name, sec, sz, attr))
        if left:
            walk(left)
        if right:
            walk(right)

    walk(0)
    return out


def walk_tree(f, base, sector, size, prefix=''):
    for name, sec, sz, attr in read_dir(f, base, sector, size):
        p = prefix + name
        if attr & 0x10:
            yield from walk_tree(f, base, sec, sz, p + '/')
        else:
            yield p, sec, sz


def main():
    f, base = open_image(sys.argv[1])
    f.seek(base + 32 * SECTOR + 20)
    root_sec, root_size = struct.unpack('<II', f.read(8))
    files = list(walk_tree(f, base, root_sec, root_size))
    if sys.argv[2] == 'list':
        for p, sec, sz in files:
            print(f'{sz:12d}  {p}')
    elif sys.argv[2] == 'get':
        want = sys.argv[3].replace('\\', '/').lower()
        for p, sec, sz in files:
            if p.lower() == want:
                f.seek(base + sec * SECTOR)
                with open(sys.argv[4], 'wb') as o:
                    o.write(f.read(sz))
                print('geschrieben', sz)
                return
        raise SystemExit('nicht gefunden: ' + sys.argv[3])


if __name__ == '__main__':
    main()
