"""Ordnet die Stellen, die die Integer-Takte eines Bildes lesen, nach Verwendung.

Aufruf: python -I ticks.py [<VA der Globalen>] [--liste] [--inc]

Die Globale (Vorgabe 0x14259CBDC: ganze Takte des Bildes, in der Pause 0)
wird von vielen TS2-Funktionen gelesen. Für einen Takt von 120 Hz zählt,
wie (docs/host.md, "120 Hz"):

  NULL_MEM   cmp/test [T] gegen 0 (Konstante oder Register, das rückwärts
             nachweislich 0 ist), danach nur je/jne/jg/jle/…
  NULL_LADE  [T] in ein Register geladen, das auf allen verfolgten Wegen nur
             mit test r,r / cmp r,0 geprüft und dann überschrieben wird
  REST       alles andere (Menge, Vergleich mit anderen Werten, Schreiben,
             Teilen, nicht nachweisbar)

Nur NULL_MEM und NULL_LADE sind reine Fragen "läuft die Zeit?" und dürfen
auf die Float-Bildzeit (0x14259CBE4, gleich 0 genau in der Pause) zeigen.
--inc schreibt src/host/tick_sites.inc (nur Adressen).
"""
import os
import re
import sys
import collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_REG, X86_OP_IMM, X86_REG_RIP
from hfdis import pe, img, B, func_of

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
data = bytes(img)

FULL = {}
for full, names in {
    'rax': ('rax', 'eax', 'ax', 'al', 'ah'), 'rbx': ('rbx', 'ebx', 'bx', 'bl', 'bh'),
    'rcx': ('rcx', 'ecx', 'cx', 'cl', 'ch'), 'rdx': ('rdx', 'edx', 'dx', 'dl', 'dh'),
    'rsi': ('rsi', 'esi', 'si', 'sil'), 'rdi': ('rdi', 'edi', 'di', 'dil'), 'rbp': ('rbp', 'ebp', 'bp', 'bpl'),
    **{f'r{i}': (f'r{i}', f'r{i}d', f'r{i}w', f'r{i}b') for i in range(8, 16)},
}.items():
    for n in names:
        FULL[n] = full
VOLATILE = {'rax', 'rcx', 'rdx', 'r8', 'r9', 'r10', 'r11'}
ZERO_JCC = {'je', 'jne', 'jg', 'jle', 'jge', 'jl', 'ja', 'jbe', 'jae', 'jb', 'js', 'jns',
            'sete', 'setne', 'cmove', 'cmovne', 'setg', 'setle', 'cmovg', 'cmovle'}


def dis1(a):
    return next(md.disasm(data[a - B:a - B + 16], a), None)


def regs_read_written(ins):
    r, w = ins.regs_access()
    return {FULL.get(ins.reg_name(x)) for x in r} - {None}, {FULL.get(ins.reg_name(x)) for x in w} - {None}


TS2_LO, TS2_HI = 0x141069B20, 0x141231770  # TS2-Block (docs/funktionen.md)


def sites(target):
    """Alle Befehle im TS2-Block mit `target` als rip-relativem Operanden. Jede
    Funktion (laut .pdata) wird von ihrem Anfang an linear dekodiert, damit
    Präfixe (REX) und Befehlsgrenzen stimmen; Kandidaten vorab per Byte-Suche."""
    t = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
    tv = t.VirtualAddress
    tb = np.frombuffer(data[tv:tv + t.Misc_VirtualSize], dtype=np.uint8)
    n = len(tb) - 4
    r = (tb[0:n].astype(np.int64) | (tb[1:n + 1].astype(np.int64) << 8) | (tb[2:n + 2].astype(np.int64) << 16) |
         (tb[3:n + 3].astype(np.int64) << 24))
    r = np.where(r >= 2 ** 31, r - 2 ** 32, r)
    funcs = set()
    for k in range(4, 12):
        for h in np.nonzero(r + np.arange(n) + tv + k == target - B)[0]:
            f = func_of(int(h) + tv)
            if f and TS2_LO <= f[0] + B < TS2_HI:
                funcs.add(f)
    res = {}
    for f in sorted(funcs):
        for ins in md.disasm(data[f[0]:f[1]], f[0] + B):
            if any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP and
                   ins.address + ins.size + op.mem.disp == target for op in ins.operands):
                res[ins.address] = ins
    return [res[a] for a in sorted(res)]


_ZERO_CACHE = {}


def zero_regs(fstart, fend):
    """Datenfluss über die Funktion: welche Register sind vor jedem Befehl auf
    allen Wegen nachweislich 0 (xor r,r / mov r,0)? Schnitt an Zusammenläufen."""
    key = (fstart, fend)
    if key in _ZERO_CACHE:
        return _ZERO_CACHE[key]
    code = {c.address: c for c in md.disasm(data[fstart:fend], fstart + B)}
    order = sorted(code)
    succ = {}
    for i, a in enumerate(order):
        c = code[a]
        nxt = order[i + 1] if i + 1 < len(order) else None
        m = c.mnemonic
        s = []
        if m == 'ret' or (m == 'jmp' and not (c.operands and c.operands[0].type == X86_OP_IMM)):
            s = []
        elif m == 'jmp':
            s = [c.operands[0].imm]
        elif m.startswith('j'):
            s = [nxt] + ([c.operands[0].imm] if c.operands and c.operands[0].type == X86_OP_IMM else [])
        else:
            s = [nxt]
        succ[a] = [x for x in s if x in code]
    ALL = None  # "noch nicht erreicht"
    state = {a: ALL for a in order}
    state[order[0]] = frozenset()
    work = [order[0]]
    while work:
        a = work.pop()
        z = set(state[a])
        c = code[a]
        _, w = regs_read_written(c)
        ops = c.operands
        is_zero = (c.mnemonic == 'xor' and len(ops) == 2 and ops[0].type == X86_OP_REG and
                   ops[1].type == X86_OP_REG and ops[0].reg == ops[1].reg) or \
                  (c.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == X86_OP_REG and
                   ops[1].type == X86_OP_IMM and ops[1].imm == 0)
        if c.mnemonic == 'call':
            z -= VOLATILE
        z -= w
        if is_zero:
            z.add(FULL.get(c.reg_name(ops[0].reg)))
        out = frozenset(z)
        for s in succ[a]:
            new = out if state[s] is ALL else (state[s] & out)
            if new != state[s]:
                state[s] = new
                work.append(s)
    _ZERO_CACHE[key] = state
    return state


def reg_is_zero_before(ins, reg):
    f = func_of(ins.address - B)
    if not f or reg is None:
        return False
    st = zero_regs(f[0], f[1]).get(ins.address)
    return st is not None and reg in st


FLAG_NEUTRAL = ('mov', 'movzx', 'movsx', 'movsxd', 'movaps', 'movups', 'movss', 'movsd', 'movd', 'movq', 'lea',
                'cvtsi2ss', 'cvtss2sd', 'cvtsd2ss', 'xorps', 'unpcklps', 'cvtps2pd', 'mulss', 'addss', 'subss',
                'divss', 'nop')


def flag_consumer(ins):
    """Der erste Befehl nach `ins`, der die Flaggen liest; flaggen-neutrale
    Befehle dazwischen werden übersprungen."""
    a = ins.address + ins.size
    for _ in range(12):
        c = dis1(a)
        if c is None:
            return None
        if c.mnemonic in ZERO_JCC:
            return c
        if c.mnemonic not in FLAG_NEUTRAL:
            return None
        a = c.address + c.size
    return None


def only_zero_tested(ins, reg, budget=60):
    """Wird das aus [T] geladene Register auf allen verfolgten Wegen nur mit
    test r,r / cmp r,0 geprüft, bevor es überschrieben wird oder der Weg endet?"""
    f = func_of(ins.address - B)
    seen = set()
    work = [(ins.address + ins.size, False)]
    tested = False
    steps = 0
    while work:
        a, _ = work.pop()
        while True:
            if a in seen or steps > budget * 4:
                break
            seen.add(a)
            steps += 1
            c = dis1(a)
            if c is None or not (f[0] + B <= a < f[1] + B):
                return False, tested
            rd, wr = regs_read_written(c)
            m = c.mnemonic
            ops = c.operands
            if reg in rd:
                if m == 'test' and len(ops) == 2 and ops[0].type == X86_OP_REG and ops[1].type == X86_OP_REG and \
                        FULL.get(c.reg_name(ops[0].reg)) == reg and FULL.get(c.reg_name(ops[1].reg)) == reg:
                    tested = True
                elif m == 'cmp' and len(ops) == 2 and ops[0].type == X86_OP_REG and \
                        FULL.get(c.reg_name(ops[0].reg)) == reg and ops[1].type == X86_OP_IMM and ops[1].imm == 0:
                    tested = True
                else:
                    return False, tested  # Menge oder anderer Vergleich
            if reg in wr and not (reg in rd):
                break  # überschrieben: dieser Weg ist fertig
            if m == 'ret':
                break
            if m == 'call':
                if reg in VOLATILE:
                    break
            if m == 'jmp':
                if ops and ops[0].type == X86_OP_IMM:
                    a = ops[0].imm
                    continue
                return False, tested  # Sprungtabelle: nicht verfolgbar
            if m.startswith('j') and ops and ops[0].type == X86_OP_IMM:
                work.append((ops[0].imm, False))
            a = c.address + c.size
    return tested, tested


def classify(ins):
    m = ins.mnemonic
    ops = ins.operands
    if m == 'cmp' and len(ops) == 2 and (ops[0].type == X86_OP_MEM or ops[1].type == X86_OP_MEM):
        other = ops[1] if ops[0].type == X86_OP_MEM else ops[0]
        zero = (other.type == X86_OP_IMM and other.imm == 0) or \
               (other.type == X86_OP_REG and reg_is_zero_before(ins, FULL.get(ins.reg_name(other.reg))))
        if zero and flag_consumer(ins) is not None:
            return 'NULL_MEM'
        return 'REST'
    if m in ('mov', 'movsxd') and ops[0].type == X86_OP_REG and ops[1].type == X86_OP_MEM:
        reg = FULL.get(ins.reg_name(ops[0].reg))
        ok, _ = only_zero_tested(ins, reg)
        return 'NULL_LADE' if ok else 'REST'
    return 'REST'


def main():
    target = 0x14259CBDC
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if args:
        target = int(args[0], 16)
    rows = []
    for ins in sites(target):
        f = func_of(ins.address - B)
        rows.append((classify(ins), ins, f[0] + B))
    cnt = collections.Counter(k for k, _, _ in rows)
    funcs = collections.defaultdict(set)
    for k, _, f in rows:
        funcs[k].add(f)
    print(f'{len(rows)} Stellen in {len({f for _, _, f in rows})} Funktionen bei 0x{target:X}')
    for k, v in cnt.most_common():
        print(f'  {k:10} {v:4} Stellen, {len(funcs[k])} Funktionen')
    if '--liste' in sys.argv:
        for k, ins, f in rows:
            print(f'{k:10} 0x{ins.address:X} (Funktion 0x{f:X}) {ins.mnemonic} {ins.op_str}')
    if '--inc' in sys.argv:
        out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'host', 'tick_sites.inc')
        with open(out, 'w', encoding='utf-8', newline='\n') as fh:
            fh.write('// Erzeugt von tools/re/ticks.py: Stellen, die die Integer-Takte 0x%X nur als\n'
                     '// Frage "läuft die Zeit?" lesen (Vergleich mit 0). {Adresse des Befehls,\n'
                     '// Länge, Lage der 32-Bit-Verschiebung im Befehl}. Nur Adressen.\n' % target)
            for k, ins, f in rows:
                if k in ('NULL_MEM', 'NULL_LADE'):
                    # Lage der Verschiebung: die rip-relative Verschiebung steht vor einem
                    # möglichen Direktwert am Ende des Befehls
                    imm = 0
                    if ins.operands[-1].type == X86_OP_IMM and ins.mnemonic in ('cmp', 'test'):
                        imm = ins.imm_size if hasattr(ins, 'imm_size') else 0
                    raw = data[ins.address - B:ins.address - B + ins.size]
                    want = (target - (ins.address + ins.size)) & 0xFFFFFFFF
                    pos = [p for p in range(ins.size - 3) if int.from_bytes(raw[p:p + 4], 'little') == want]
                    assert len(pos) == 1, hex(ins.address)
                    fh.write(f'    {{0x{ins.address:X}, {ins.size}, {pos[0]}}},  // {k} {ins.mnemonic} {ins.op_str}\n')
        print('geschrieben:', os.path.normpath(out))


if __name__ == '__main__':
    main()
