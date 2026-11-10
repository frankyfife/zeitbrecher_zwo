"""Holt die Fehlerbehebungen von TS2 Redux aus dessen DLLs (eigene Kopie).

Aufruf: python -I redux_patches.py <TS2Redux-Ordner> <game-Ordner> <Homefront2_Release.exe>

TS2 Redux (github.com/HFTSRedux/TS2Redux, GPLv3) behebt Fehler des
TS2-Ports in Homefront: The Revolution mit kleinen DLLs, die beim Start
Bytes in Homefront2_Release.exe schreiben (GetModuleHandleA(NULL) + Abstand,
VirtualProtect, je Byte ein mov). Dieses Skript liest diese Schreibzugriffe
statisch aus dem Maschinencode (capstone; die DLLs werden nicht ausgeführt)
und legt sie als game/redux.bin ab. ts2.exe wendet sie beim Start an
(src/host/redux.cpp), je Gruppe abschaltbar in ts2.ini [redux].

Die Patch-Daten gehören den Redux-Autoren und kommen deshalb nicht ins
Repository; nur dieses Skript.

Format von redux.bin: "TS2R", u32 Anzahl Gruppen; je Gruppe u8 Namenslänge,
Name, u32 Anzahl Einträge; je Eintrag u32 RVA, u16 Länge, Bytes.
"""
import os
import struct
import sys

import pefile
from capstone import CS_ARCH_X86, CS_MODE_64, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_RBP, X86_REG_RIP, X86_REG_RSP

# Gruppe in ts2.ini -> DLL
GROUPS = [
    ('spielfehler', 'GameFixesByFanotoAndDevilDwarf.dll'),  # Reaper-Spawn, Regen auf der Kamera
    ('unsichtbar', 'InvisibleFixByDevilDwarf.dll'),          # Unsichtbarkeitsfehler
    ('anzeige', 'DevilDwarfUI.dll'),                         # HUD/Textanzeige
    ('menue', 'DevilDwarfMenu.dll'),                         # Menü-Layout
]

WIDE = {'al': 'rax', 'eax': 'rax', 'ax': 'rax', 'dl': 'rdx', 'edx': 'rdx', 'cl': 'rcx', 'ecx': 'rcx',
        'bl': 'rbx', 'ebx': 'rbx', 'sil': 'rsi', 'esi': 'rsi', 'dil': 'rdi', 'edi': 'rdi'}
for n in range(8, 16):
    for suf in ('b', 'w', 'd'):
        WIDE[f'r{n}{suf}'] = f'r{n}'


def extract(path):
    pe = pefile.PE(path)
    base = pe.OPTIONAL_HEADER.ImageBase
    text = next(s for s in pe.sections if s.Name.startswith(b'.text'))
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    wide = lambda r: WIDE.get(md.reg_name(r), md.reg_name(r))
    vals, patches = {}, {}
    for i in md.disasm(text.get_data(), base + text.VirtualAddress):
        ops = i.operands
        if i.mnemonic in ('mov', 'movzx', 'movsx') and len(ops) == 2 and ops[0].type == X86_OP_REG:
            d = wide(ops[0].reg)
            if ops[1].type == X86_OP_IMM:
                vals[d] = ops[1].imm
            elif ops[1].type == X86_OP_MEM and ops[1].mem.base == X86_REG_RIP:
                va = i.address + i.size + ops[1].mem.disp
                vals[d] = int.from_bytes(pe.get_data(va - base, ops[1].size), 'little')
            elif ops[1].type == X86_OP_REG and wide(ops[1].reg) in vals:
                vals[d] = vals[wide(ops[1].reg)]
            else:
                vals.pop(d, None)
        if (i.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == X86_OP_MEM
                and ops[0].mem.base not in (X86_REG_RIP, X86_REG_RSP, X86_REG_RBP)
                and ops[0].mem.disp >= 0x10000):
            if ops[1].type == X86_OP_IMM:
                v = ops[1].imm
            elif ops[1].type == X86_OP_REG and wide(ops[1].reg) in vals:
                v = vals[wide(ops[1].reg)]
            else:
                raise SystemExit(f'{os.path.basename(path)}: Wert unklar bei {i.address:#x}: {i.mnemonic} {i.op_str}')
            v &= (1 << (8 * ops[0].size)) - 1
            for n in range(ops[0].size):
                patches[ops[0].mem.disp + n] = (v >> (8 * n)) & 0xFF
    runs = []
    for rva in sorted(patches):
        if runs and runs[-1][0] + len(runs[-1][1]) == rva:
            runs[-1][1].append(patches[rva])
        else:
            runs.append([rva, [patches[rva]]])
    return runs


def data_deps(exe, runs):
    """Bytes außerhalb der Änderungen, die die geänderten Befehle lesen.

    Redux lenkt rip-relative Konstanten-Ladungen (movss xmm, [rip+x]) auf
    passende Bytes irgendwo in Homefronts EXE um, auch außerhalb des TS2-Teils
    (ts2.bin hat diese Seiten nicht). Die geänderten Befehle werden im
    geänderten Bild dekodiert (Funktionsgrenzen aus .pdata, Code-Stücke ab
    ihrem Anfang); für jedes rip-relative Speicherziel kommen 16 Bytes aus der
    eigenen EXE mit. Sprünge/Aufrufe in fremden Code werden gemeldet.
    """
    img = bytearray(exe.get_memory_mapped_image())
    patched = set()
    for rva, bs in runs:
        img[rva:rva + len(bs)] = bytes(bs)
        patched.update(range(rva, rva + len(bs)))
    funcs = sorted((f.struct.BeginAddress, f.struct.EndAddress)
                   for f in getattr(exe, 'DIRECTORY_ENTRY_EXCEPTION', []))
    starts = [b for b, _ in funcs]
    import bisect
    ranges = set()
    for rva, bs in runs:
        i = bisect.bisect_right(starts, rva) - 1
        if i >= 0 and funcs[i][0] <= rva < funcs[i][1]:
            ranges.add(funcs[i])
        else:
            ranges.add((rva, rva + len(bs)))  # Code-Stück oder Daten
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    base = exe.OPTIONAL_HEADER.ImageBase
    deps, foreign = {}, []
    for a, e in sorted(ranges):
        for ins in md.disasm(bytes(img[a:e]), a):
            if not patched.intersection(range(ins.address, ins.address + ins.size)):
                continue
            for op in ins.operands:
                if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                    t = ins.address + ins.size + op.mem.disp
                    if not patched.issuperset(range(t, t + 16)):
                        deps[t] = bytes(img[t:t + 16])
                if ins.mnemonic in ('call', 'jmp') and op.type == X86_OP_IMM:
                    t = op.imm
                    if t not in patched:
                        foreign.append((ins.address, t))
    return [[t, list(b)] for t, b in sorted(deps.items())], foreign


def main():
    src, game = sys.argv[1], sys.argv[2]
    exe = pefile.PE(sys.argv[3])
    dlls = os.path.join(src, 'Install', 'App-DLLs', 'TS2Redux')
    out = [b'TS2R', struct.pack('<I', len(GROUPS))]
    for name, dll in GROUPS:
        runs = extract(os.path.join(dlls, dll))
        deps, foreign = data_deps(exe, runs)
        for at, t in foreign:
            print(f'  Sprung/Aufruf bei {at + 0x140000000:#x} nach {t + 0x140000000:#x} (bestehender Code, nicht kopiert)')
        if deps:
            print(f'  {len(deps)} Konstanten aus der EXE mitgenommen')
        runs = deps + runs  # Konstanten zuerst, Änderungen danach (überschreiben)
        out += [struct.pack('<B', len(name)), name.encode(), struct.pack('<I', len(runs))]
        for rva, bs in runs:
            out += [struct.pack('<IH', rva, len(bs)), bytes(bs)]
        print(f'{name}: {len(runs)} Stellen, {sum(len(b) for _, b in runs)} Bytes ({dll})')
    with open(os.path.join(game, 'redux.bin'), 'wb') as f:
        f.write(b''.join(out))
    print('geschrieben:', os.path.join(game, 'redux.bin'))


if __name__ == '__main__':
    main()
