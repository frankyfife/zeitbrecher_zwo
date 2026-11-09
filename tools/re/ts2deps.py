"""Was braucht der TS2-Code von außerhalb seines Blocks?

Listet für alle Funktionen im TS2-Block (docs/funktionen.md):
  * direkte Aufrufe/Sprünge nach außen (Ziel, Anzahl Aufrufer)
  * Aufrufe über die Importtabelle (DLL!Funktion)
  * RIP-relative Datenzugriffe auf .data außerhalb der von TS2 allein
    benutzten Adressen (Kandidaten für Engine-Globale wie gEnv)
  * für geladene Engine-Zeiger: welche vtable-Offsets darauf gerufen werden
Aufruf: python -I ts2deps.py > re/out/ts2_deps.txt
"""
import collections, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pefile
from hfdis import pe, img, B, funcs, md
from capstone import x86
TS2_LO, TS2_HI = 0x141069B20 - B, 0x141231770 - B
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
iat = {}
for d in pe.DIRECTORY_ENTRY_IMPORT:
    for imp in d.imports:
        iat[imp.address - B] = f"{d.dll.decode()}!{(imp.name or b'#%d' % imp.ordinal).decode()}"
sec = {s.Name.rstrip(b'\0').decode(): (s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize) for s in pe.sections}
def in_sec(a, n): lo, hi = sec[n]; return lo <= a < hi
md.detail = True
ext_calls = collections.Counter(); ext_callers = collections.defaultdict(set)
imp_calls = collections.Counter()
data_refs = collections.defaultdict(set)   # .data address -> TS2 functions
ptr_vcalls = collections.defaultdict(collections.Counter)  # global -> vtable offset -> count
ts2_funcs = [f for f in funcs if TS2_LO < f[0] < TS2_HI]
for start, end, _ in ts2_funcs:
    loaded = {}  # reg -> global loaded from
    vt = {}      # reg -> global whose vtable it holds
    for ins in md.disasm(bytes(img[start:end]), B + start):
        ops = ins.operands
        tgt = None
        if ins.mnemonic in ('call', 'jmp') and ops and ops[0].type == x86.X86_OP_IMM:
            t = ops[0].imm - B
            if not (start <= t < end) and not (TS2_LO < t < TS2_HI):
                ext_calls[t] += 1; ext_callers[t].add(start)
        for op in ops:
            if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP:
                a = ins.address + ins.size + op.mem.disp - B
                if a in iat and ins.mnemonic in ('call', 'jmp'):
                    imp_calls[iat[a]] += 1
                elif in_sec(a, '.data'):
                    data_refs[a].add(start)
                    if ins.mnemonic == 'mov' and ops[0].type == x86.X86_OP_REG:
                        loaded[ops[0].reg] = a
        # mov rax, [reg] -> vtable of loaded global ; call [rax+off]
        if ins.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == x86.X86_OP_REG and ops[1].type == x86.X86_OP_MEM \
                and ops[1].mem.disp == 0 and ops[1].mem.base in loaded:
            vt[ops[0].reg] = loaded[ops[1].mem.base]
        if ins.mnemonic in ('call', 'jmp') and ops and ops[0].type == x86.X86_OP_MEM and ops[0].mem.base in vt:
            ptr_vcalls[vt[ops[0].mem.base]][ops[0].mem.disp] += 1
print(f"TS2-Funktionen: {len(ts2_funcs)}")
print(f"\n# Direkte Aufrufe nach außen: {len(ext_calls)} Ziele")
for t, n in ext_calls.most_common(): print(f"  0x{t + B:X}  {n:4d} Aufrufe aus {len(ext_callers[t])} Fkt.")
print(f"\n# Importe: {len(imp_calls)}")
for k, n in sorted(imp_calls.items()): print(f"  {k}  {n}")
print(f"\n# Engine-Zeiger (Globale, über deren vtable TS2 ruft): {len(ptr_vcalls)}")
for g, c in sorted(ptr_vcalls.items()):
    print(f"  0x{g + B:X}: " + ", ".join(f"+0x{o:X}×{n}" for o, n in sorted(c.items())))
# .data refs: summarise ranges outside TS2-only clusters
print(f"\n# .data-Adressen, die TS2 anfasst: {len(data_refs)}")
lo = min(data_refs); hi = max(data_refs)
print(f"  Spanne 0x{lo + B:X} – 0x{hi + B:X}")
buckets = collections.Counter((a >> 16) for a in data_refs)
for k in sorted(buckets): print(f"  0x{(k << 16) + B:X}: {buckets[k]}")
