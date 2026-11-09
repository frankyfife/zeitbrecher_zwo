"""Erzeugt src/host/ts2_manifest.inc: was ts2.bin aus Homefront2_Release.exe braucht.

Das Manifest enthält nur Adressen und Namen, keinen Code und keine Daten des
Spiels:
  * kText:     Bereiche von .text (der TS2-Block und einzelne CRT-Helfer)
  * kPages:    Seiten von .rdata/.data mit Rohdaten, die dieser Code anfasst
               (jede referenzierte Seite und die folgende, für Tabellen, die
               über ihr Ende hinaus gelesen werden)
  * kBss:      der Bereich von .data ohne Rohdaten (wird genullt bereitgestellt)
  * kImports:  Importslots, die der Code benutzt (Adresse, DLL, Name)
  * kExternal: Funktionen außerhalb, die der Code direkt ruft; ts2.exe legt an
               jede Adresse einen Sprung auf eigenen Ersatzcode
Aufruf: python -I make_manifest.py > ../../src/host/ts2_manifest.inc
"""
import collections, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pefile
from capstone import x86
from hfdis import pe, img, B, funcs, md

md.detail = True
TS2_LO, TS2_HI = 0x141069B20 - B, 0x141231770 - B
# CRT-Helfer, die unverändert mitkommen (kein Engine-Bezug):
#   0x141C50CA0 __chkstk (eigene Aufrufkonvention, nicht in C nachzubauen)
EXTRA_TEXT = [(0x141C50CA0 - B, 0x141C50CA0 - B + 0x50)]
PAGE = 0x1000

pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
iat = {}
for d in pe.DIRECTORY_ENTRY_IMPORT:
    for imp in d.imports:
        iat[imp.address - B] = (d.dll.decode().lower(), (imp.name or b'#%d' % imp.ordinal).decode())
secs = {s.Name.rstrip(b'\0').decode(): s for s in pe.sections}
text, rdata, data = secs['.text'], secs['.rdata'], secs['.data']

def sec_of(a):
    for n, s in secs.items():
        if s.VirtualAddress <= a < s.VirtualAddress + s.Misc_VirtualSize:
            return n
    return None

ranges = [(s, e) for s, e, _ in funcs if TS2_LO < s < TS2_HI]
lo = min(s for s, _ in ranges)
hi = max(e for _, e in ranges)
text_ranges = [(lo & ~0xF, hi)] + EXTRA_TEXT

pages = set()
imports = {}
external = collections.Counter()
# Funktionsweise disassemblieren (.pdata); eine lineare Sweep bricht an
# Datenbytes im Code (Sprungtabellen, Füllbytes) ab.
code_funcs = [(s, e) for s, e, _ in funcs if any(a <= s < b for a, b in text_ranges)]
for s, e in code_funcs:
    for ins in md.disasm(bytes(img[s:e]), B + s):
        ops = ins.operands
        if ins.mnemonic in ('call', 'jmp') and ops and ops[0].type == x86.X86_OP_IMM:
            t = ops[0].imm - B
            if not any(a <= t < b for a, b in text_ranges):
                external[t] += 1
        for op in ops:
            if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP:
                a = ins.address + ins.size + op.mem.disp - B
                if a in iat:
                    imports[a] = iat[a]
                    continue
                n = sec_of(a)
                if n == '.text':
                    if not any(x <= a < y for x, y in text_ranges):
                        external[a] += 1  # Adresse einer fremden Funktion (lea)
                elif n in ('.rdata', '.data'):
                    p = a & ~(PAGE - 1)
                    pages.add(p)
                    pages.add(p + PAGE)

def raw_end(s):
    return s.VirtualAddress + min(s.SizeOfRawData, s.Misc_VirtualSize)

# .data: vollständig (TS2 erreicht große Felder über berechnete Indizes,
# eine Auswahl nach direkten Verweisen verfehlt sie). .rdata: referenzierte
# Seiten, Lücken bis 64 KB dazwischen werden mitgenommen (Tabellen).
GAP = 0x40000
out_pages = list(range(data.VirtualAddress, (raw_end(data) + PAGE - 1) & ~(PAGE - 1), PAGE))
rd_pages = sorted(p for p in pages if rdata.VirtualAddress <= p < raw_end(rdata))
for i, p in enumerate(rd_pages):
    out_pages.append(p)
    if i + 1 < len(rd_pages) and rd_pages[i + 1] - p <= GAP:
        out_pages.extend(range(p + PAGE, rd_pages[i + 1], PAGE))
out_pages = sorted(set(out_pages))
# zusammenhängende Seiten zu Bereichen
merged = []
for p in out_pages:
    if merged and merged[-1][1] == p:
        merged[-1][1] = p + PAGE
    else:
        merged.append([p, p + PAGE])
for m in merged:  # nicht über das Rohdatenende des Abschnitts
    for s in (rdata, data):
        if s.VirtualAddress <= m[0] < raw_end(s):
            m[1] = min(m[1], (raw_end(s) + PAGE - 1) & ~(PAGE - 1))

print("// Erzeugt von tools/re/make_manifest.py – nicht von Hand ändern.")
print("// Adressen als RVA in Homefront2_Release.exe (TimeDateStamp 0x58E252DC).")
print("#pragma once")
print("namespace manifest {")
print("struct Range { uint32_t rva, size; };")
print("struct Import { uint32_t slot; const char* dll; const char* name; };")
print("struct External { uint32_t rva; uint32_t uses; };")
print("constexpr uint32_t kTimeDateStamp = 0x%08X;" % pe.FILE_HEADER.TimeDateStamp)
print("constexpr uint32_t kSizeOfImage = 0x%08X;" % pe.OPTIONAL_HEADER.SizeOfImage)
print("constexpr Range kText[] = {")
for s, e in text_ranges:
    print("  {0x%08X, 0x%X}," % (s, e - s))
print("};")
print("constexpr Range kPages[] = {")
print("  {0x00000000, 0x1000}, // PE-Kopf: Windows-Funktionen bekommen die Modulbasis als HINSTANCE")
for s, e in merged:
    print("  {0x%08X, 0x%X}, // %s" % (s, e - s, sec_of(s)))
print("};")
bss_lo = (raw_end(data) + PAGE - 1) & ~(PAGE - 1)
print("constexpr Range kBss = {0x%08X, 0x%X};" % (bss_lo, data.VirtualAddress + data.Misc_VirtualSize - bss_lo))
print("constexpr Import kImports[] = {")
for a in sorted(imports):
    print('  {0x%08X, "%s", "%s"},' % (a, *imports[a]))
print("};")
print("constexpr External kExternal[] = {")
for a in sorted(external):
    print("  {0x%08X, %d}," % (a, external[a]))
print("};")
print("}  // namespace manifest")
tot_text = sum(e - s for s, e in text_ranges)
tot_pages = sum(e - s for s, e in merged)
print(f"// Umfang: Code {tot_text // 1024} KB, Daten {tot_pages // 1024} KB, {len(imports)} Importe, "
      f"{len(external)} fremde Funktionen", file=sys.stderr)
