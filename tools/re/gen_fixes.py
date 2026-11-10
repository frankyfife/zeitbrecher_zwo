"""Erzeugt src/host/fixes.inc: die Fehlerbehebungen nach TS2 Redux als
Tabelle in unserem Code (Entwicklerwerkzeug, einmalig ausgeführt).

Aufruf: python -I tools/re/gen_fixes.py <TS2Redux-Ordner> <Homefront2_Release.exe> src/host/fixes.inc

TS2 Redux (github.com/HFTSRedux/TS2Redux, GPLv3; Dank an Fanoto und
DevilDwarf) behebt Fehler des TS2-Ports mit DLLs, die Bytes in Homefronts EXE
schreiben. Dieses Skript liest diese Schreibzugriffe statisch aus dem
Maschinencode der DLLs (capstone; nichts wird ausgeführt) und schreibt sie
als Tabelle; ts2.exe braucht danach weder Redux noch dessen Dateien.

- Je Stelle: RVA, neue Bytes und die CRC32 der ursprünglichen Bytes (ts2.exe
  prüft sie und lässt eine abweichende Stelle aus). Stellen außerhalb von
  ts2.bin (Code-Stücke in Homefronts Füllbereichen) haben CRC 0.
- Redux lenkt einige Konstanten-Ladungen (movss xmm, [rip+x]) auf passende
  Bytes irgendwo in Homefronts EXE um. Liegt das Ziel außerhalb von ts2.bin,
  steht hier stattdessen der Wert (vier Floats); ts2.exe legt ihn in einer
  eigenen Seite ab und richtet den Befehl darauf aus.
"""
import binascii
import bisect
import os
import re
import struct
import sys

import pefile
from capstone import CS_ARCH_X86, CS_MODE_64, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_RBP, X86_REG_RIP, X86_REG_RSP


GROUPS = [
    ('spielfehler', 'GameFixesByFanotoAndDevilDwarf.dll', 'Reaper-Spawn, Regen auf der Kamera'),
    ('unsichtbar', 'InvisibleFixByDevilDwarf.dll', 'Unsichtbarkeitsfehler'),
    ('anzeige', 'DevilDwarfUI.dll', 'HUD- und Textanzeige'),
    ('menue', 'DevilDwarfMenu.dll', 'Menü-Layout wie auf der Konsole'),
]
BASE = 0x140000000

# Byte-Schreibzugriffe einer Redux-DLL (GetModuleHandleA(NULL) + Abstand, je Byte ein mov)
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




def manifest_ranges(path):
    s = open(path, encoding='utf-8', errors='replace').read()
    out = []
    for block in ('kText', 'kPages'):
        body = s[s.index(f'constexpr Range {block}[]'):]
        body = body[:body.index('};')]
        out += [(int(a, 16), int(b, 16)) for a, b in re.findall(r'\{0x([0-9A-F]+), 0x([0-9A-F]+)\}', body)]
    m = re.search(r'kBss = \{0x([0-9A-F]+), 0x([0-9A-F]+)\}', s)
    out.append((int(m.group(1), 16), int(m.group(2), 16)))
    return out


def main():
    src, exe_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ranges = manifest_ranges(os.path.join(root, 'src', 'host', 'ts2_manifest.inc'))
    in_bin = lambda rva, n=1: any(a <= rva and rva + n <= a + s for a, s in ranges)
    exe = pefile.PE(exe_path)
    img = bytearray(exe.get_memory_mapped_image())
    funcs = sorted((f.struct.BeginAddress, f.struct.EndAddress) for f in exe.DIRECTORY_ENTRY_EXCEPTION)
    starts = [b for b, _ in funcs]
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    dlls = os.path.join(src, 'Install', 'App-DLLs', 'TS2Redux')

    lines = ['// Erzeugt von tools/re/gen_fixes.py aus TS2 Redux (github.com/HFTSRedux/TS2Redux,',
             '// GPLv3, Dank an Fanoto und DevilDwarf). Nicht von Hand ändern.', '']
    entries, consts = [], []
    for name, dll, title in GROUPS:
        runs = extract(os.path.join(dlls, dll))
        patched = bytearray(img)
        changed = set()
        n_bytes = 0
        for rva, bs in runs:
            orig = bytes(img[rva:rva + len(bs)])
            if orig == bytes(bs):
                continue  # schreibt nur, was schon dasteht
            patched[rva:rva + len(bs)] = bytes(bs)
            changed.update(range(rva, rva + len(bs)))
            crc = binascii.crc32(orig) if in_bin(rva, len(bs)) else 0
            arr = f'k{name}_{rva:x}'
            lines.append(f'static const uint8_t {arr}[] = {{{", ".join(f"0x{b:02X}" for b in bs)}}};')
            entries.append(f'    {{0x{rva:08X}, 0x{crc:08X}, {len(bs)}, "{name}", {arr}}},')
            n_bytes += len(bs)
        # rip-relative Ziele der geänderten Befehle außerhalb von ts2.bin
        areas = set()
        for rva in sorted(changed):
            i = bisect.bisect_right(starts, rva) - 1
            if i >= 0 and funcs[i][0] <= rva < funcs[i][1]:
                areas.add(funcs[i])
        for rva, bs in runs:
            if not in_bin(rva, len(bs)):
                areas.add((rva, rva + len(bs)))  # Code-Stück
        n_const = 0
        for a, e in sorted(areas):
            for ins in md.disasm(bytes(patched[a:e]), a):
                if not changed.intersection(range(ins.address, ins.address + ins.size)):
                    continue
                for op in ins.operands:
                    if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                        t = ins.address + ins.size + op.mem.disp
                        # Nur unveränderliche Werte (in .text/.rdata) umziehen, die
                        # der Befehl liest; beschreibbare Variablen bleiben, wo sie
                        # sind. Auch Ziele in ts2.bin ziehen um: dort können
                        # eigene Umlenkungen Bytes ändern, Redux meinte die Werte
                        # der unveränderten EXE.
                        sec = next((s for s in exe.sections
                                    if s.VirtualAddress <= t < s.VirtualAddress + s.Misc_VirtualSize), None)
                        if sec is None or sec.Characteristics & 0x80000000:  # IMAGE_SCN_MEM_WRITE
                            continue
                        if op is not ins.operands[-1]:
                            continue  # Ziel wird geschrieben, nicht gelesen
                        n = max(1, min(4, op.size // 4))  # nur so viele Floats, wie der Befehl liest
                        vals = list(struct.unpack(f'<{n}f', bytes(img[t:t + 4 * n]))) + [0.0] * (4 - n)
                        disp_at = ins.address + ins.disp_offset
                        consts.append(f'    {{0x{disp_at:08X}, 0x{ins.address + ins.size:08X}, '
                                      f'{{{", ".join(repr(float(v)) + "f" for v in vals)}}}, "{name}"}},'
                                      f'  // {ins.mnemonic} {ins.op_str}')
                        n_const += 1
        print(f'{name}: {len(entries)} Stellen bisher, {n_bytes} Bytes, {n_const} Konstanten ({title})')
    lines += ['', 'struct FixBytes {', '    uint32_t rva, crc;  // crc der ursprünglichen Bytes, 0 = außerhalb von ts2.bin',
              '    uint16_t len;', '    const char* group;', '    const uint8_t* bytes;', '};',
              'static const FixBytes kFixBytes[] = {'] + entries + ['};', '',
              '// Konstanten-Ladungen: Verschiebung bei disp_rva, Befehlsende next_rva, Wert',
              'struct FixConst {', '    uint32_t disp_rva, next_rva;', '    float value[4];',
              '    const char* group;', '};', 'static const FixConst kFixConsts[] = {'] + consts + ['};', '']
    open(out_path, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
    print('geschrieben:', out_path, len(entries), 'Stellen,', len(consts), 'Konstanten')


if __name__ == '__main__':
    main()
