"""Sucht Float-/Double-Konstanten in .rdata/.data und die TS2-Funktionen, die sie lesen.

Aufruf: python -I constref.py <wert> [<wert>...]   (Werte als Python-Ausdruck, z. B. 20/7)
Nur Verweise aus dem TS2-Block (docs/funktionen.md) werden gezeigt; --all zeigt alle.
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from xref import pe, img, B, func_of, rels
TS2_LO, TS2_HI = 0x141069B20, 0x141231770
R = rels()
tv = [s for s in pe.sections if s.Name.startswith(b'.text')][0].VirtualAddress
show_all = '--all' in sys.argv
data = bytes(img)
for expr in [a for a in sys.argv[1:] if a != '--all']:
    v = eval(expr, {})
    for kind, pat in (('f32', struct.pack('<f', v)), ('f64', struct.pack('<d', v))):
        for sec in pe.sections:
            if sec.Name.rstrip(b'\0') not in (b'.rdata', b'.data'):
                continue
            lo, hi = sec.VirtualAddress, sec.VirtualAddress + sec.Misc_VirtualSize
            o = data.find(pat, lo, hi)
            while o != -1:
                if o % 4 == 0:
                    hits = np.nonzero(R == o)[0]
                    fs = sorted({func_of(int(h) + tv)[0] + B for h in hits if func_of(int(h) + tv)})
                    ts = [f for f in fs if TS2_LO < f < TS2_HI]
                    if ts or (show_all and fs):
                        print(f'{expr} {kind} @0x{o + B:X} {sec.Name.rstrip(bytes(1)).decode()}: '
                              f'{len(ts)} TS2-Fkt. {" ".join(hex(f) for f in ts[:12])}'
                              + (f' | andere {len(fs) - len(ts)}' if show_all else ''))
                o = data.find(pat, o + 1, hi)
