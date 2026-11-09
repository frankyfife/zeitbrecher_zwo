"""TS2-Funktionen, die alle angegebenen Float-Konstanten lesen (Schnittmenge).

Aufruf: python -I constand.py <wert> <wert> [...]
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from xref import pe, img, B, func_of, rels
TS2_LO, TS2_HI = 0x141069B20, 0x141231770
R = rels(); tv = [s for s in pe.sections if s.Name.startswith(b'.text')][0].VirtualAddress
data = bytes(img)
def users(v):
    out = set()
    for pat in (struct.pack('<f', v), struct.pack('<d', v)):
        for sec in pe.sections:
            if sec.Name.rstrip(b'\0') not in (b'.rdata', b'.data'): continue
            lo, hi = sec.VirtualAddress, sec.VirtualAddress + sec.Misc_VirtualSize
            o = data.find(pat, lo, hi)
            while o != -1:
                if o % 4 == 0:
                    for h in np.nonzero(R == o)[0]:
                        f = func_of(int(h) + tv)
                        if f and TS2_LO < f[0] + B < TS2_HI: out.add((f[0] + B, f[1] - f[0]))
                o = data.find(pat, o + 1, hi)
    return out
if __name__ == '__main__':
    sets = [users(eval(a, {})) for a in sys.argv[1:]]
    common = set.intersection(*sets)
    for f, sz in sorted(common): print(hex(f), sz)
