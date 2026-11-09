"""Zählt in TS2-Funktionen, die eine Konstante lesen, Float-Zugriffe auf bestimmte Strukturoffsets.

Aufruf: python -I scanoff.py <konstante> <offset> [<offset>...]
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from constand import users
from hfdis import md, img, B
const = eval(sys.argv[1], {})
offs = [int(o, 16) for o in sys.argv[2:]]
for f, sz in sorted(users(const)):
    c = {o: [0, 0] for o in offs}
    for ins in md.disasm(bytes(img[f - B:f - B + sz]), f):
        if not ins.mnemonic.startswith(('movss', 'addss', 'subss', 'mulss', 'comiss')): continue
        for o in offs:
            m = re.search(r'\[r\w+ \+ 0x%x\]' % o, ins.op_str)
            if m:
                c[o][0 if ins.op_str.startswith('dword ptr [') else 1] += 1
    if any(sum(v) for v in c.values()):
        print(hex(f), sz, ' '.join('+%x w%d r%d' % (o, v[0], v[1]) for o, v in c.items()))
