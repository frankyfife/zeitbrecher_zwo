"""Welche Felder liest TS2 aus der Struktur, auf die eine Globale zeigt?

Aufruf: python -I structuse.py <VA der Globalen> [...]
Sucht im TS2-Block `mov reg, [rip -> global]` und danach `[reg + off]`
(bis reg überschrieben wird) und zählt die Offsets; bei Feldern, über die
anschließend eine vtable gerufen wird, auch die Methodenoffsets.
"""
import collections, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hfdis import img, B, funcs, md
from capstone import x86
md.detail = True
LO, HI = 0x141069B20 - B, 0x141231770 - B
for g in [int(a, 16) for a in sys.argv[1:]]:
    fields = collections.Counter(); methods = collections.defaultdict(collections.Counter)
    for st, en, _ in funcs:
        if not (LO < st < HI): continue
        base = {}   # reg -> 'g'
        fld = {}    # reg -> field offset (loaded [g+off])
        vt = {}     # reg -> field offset whose vtable it holds
        for ins in md.disasm(bytes(img[st:en]), B + st):
            ops = ins.operands
            w = ops[0].reg if ops and ops[0].type == x86.X86_OP_REG else None
            src = ops[1] if len(ops) > 1 else None
            newb = newf = newv = None
            if ins.mnemonic == 'mov' and w and src is not None and src.type == x86.X86_OP_MEM:
                m = src.mem
                if m.base == x86.X86_REG_RIP and ins.address + ins.size + m.disp == g: newb = True
                elif m.base in base: fields[m.disp] += 1; newf = m.disp
                elif m.base in fld and m.disp == 0: newv = fld[m.base]
            for op in ops:
                if op.type == x86.X86_OP_MEM and op.mem.base in base and not (ins.mnemonic == 'mov' and op is src):
                    fields[op.mem.disp] += 1
            if ins.mnemonic in ('call', 'jmp') and ops and ops[0].type == x86.X86_OP_MEM and ops[0].mem.base in vt:
                methods[vt[ops[0].mem.base]][ops[0].mem.disp] += 1
            if w:
                for d in (base, fld, vt): d.pop(w, None)
                if newb: base[w] = 1
                if newf is not None: fld[w] = newf
                if newv is not None: vt[w] = newv
            if ins.mnemonic in ('call',):
                for r in (x86.X86_REG_RAX, x86.X86_REG_RCX, x86.X86_REG_RDX, x86.X86_REG_R8, x86.X86_REG_R9, x86.X86_REG_R10, x86.X86_REG_R11):
                    for d in (base, fld, vt): d.pop(r, None)
    print(f"0x{g:X}: Felder " + ", ".join(f"+0x{o:X}×{n}" for o, n in sorted(fields.items())))
    for f, c in sorted(methods.items()):
        print(f"   über +0x{f:X}: Methoden " + ", ".join(f"+0x{o:X}×{n}" for o, n in sorted(c.items())))
