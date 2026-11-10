"""Übernimmt die hochaufgelösten Texturen von TS2 Redux nach game/hd/.

Aufruf: python -I tools/re/hd_textures.py <ts2_redux.pak> <game-Ordner>

ts2_redux.pak (github.com/HFTSRedux/TS2Redux, HD-Textures) ist ein
gewöhnliches ZIP mit ts2/textures/**/*.rgb (SGI). ts2.exe liest Dateien unter
game/hd/ vor den Originalen (src/host/fixes.cpp). Ausgelassen werden Logo,
Ladebilder und Banner mit Redux-Schriftzug sowie die Xbox-Tastensymbole.
Eigene Texturen (tools/hd, docs/hd-texturen.md) legt man in denselben Ordner.
"""
import os
import re
import sys
import zipfile

SKIP = re.compile(r'(.*logo|ts2load\d|ts2banner\d|xb.*)\.rgb$')


def main():
    pak, game = sys.argv[1], sys.argv[2]
    dest = os.path.realpath(os.path.join(game, 'hd'))
    n = 0
    with zipfile.ZipFile(pak) as z:
        for info in z.infolist():
            target = os.path.realpath(os.path.join(dest, info.filename))
            if info.is_dir() or not target.startswith(dest + os.sep) or SKIP.match(os.path.basename(info.filename)):
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with z.open(info) as i, open(target, 'wb') as o:
                o.write(i.read())
            n += 1
    print(f'{n} Texturen nach {dest}')


if __name__ == '__main__':
    main()
