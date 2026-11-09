"""Bestimmt die TS2-Funktionen in Homefront2_Release.exe.

Eingabe: der Ghidra-Export in re/out/ (functions.tsv, calls.tsv,
string_refs.tsv, von re/ghidra_scripts/ExportFunctions.java).
Ausgabe: re/out/ts2_functions.tsv (entry, size, name, Grund, Strings) und
eine Übersicht auf stdout.

Befund (docs/funktionen.md): TS2 ist eine eigene Bibliothek und liegt als ein
zusammenhängender Block in .text, eingefasst von CryNetwork (darunter) und
CryRender (darüber). Vorgehen:
 1. Saat TS2: Funktionen mit TS2-typischen Strings (Quellpfade
    Code\\TS2\\ts2redux, host0:, ob/, .xbr, .pob, TimeSplitters ...).
 2. Saat Engine: Funktionen mit anderen __FILE__-Pfaden, C++-Klassennamen
    oder CRYENGINE-Begriffen in ihren Strings.
 3. Block: von der letzten Engine-Funktion vor der ersten TS2-Saat bis zur
    ersten Engine-Funktion nach der letzten TS2-Saat (jeweils exklusiv).
    Alles dazwischen gilt als TS2, außer Engine-Saat.
 4. Außerhalb des Blocks: Funktionen, die nur von TS2 gerufen werden
    (ausgebreitet, bis nichts dazukommt) – meist geteilte Hilfen, die der
    Linker anderswo abgelegt hat.

Aufruf: python -I classify.py [re/out]
"""
import bisect
import collections
import csv
import os
import re
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "re", "out")

TS2_STR = re.compile(
    r"(?i)code\\ts2\\|ts2redux|^host0:|^ob/|\.xbr$|\.pob$|timesplitter|^ts2/|^TS Tick$|"
    r"arcade (league|custom|awards|series|level|match)")
ENGINE_STR = re.compile(
    r"(?i)^d:\\work\\dsdb\\(?!code\\ts2\\)|\b[CI][A-Z][A-Za-z0-9]+::[A-Za-z~]|\bcry[a-z]|\blua\b|flowgraph|"
    r"scaleform|\bgfx|fmod|steam|mannequin|\[RMI\]|SENDBIND|^netID$|"
    r"^\$[A-Za-z]|system shader|occlusion quer|async dips")


def read_tsv(name):
    with open(os.path.join(OUT, name), encoding="utf-8", errors="replace", newline="") as f:
        r = csv.reader(f, delimiter="\t", quoting=csv.QUOTE_NONE)
        next(r)
        for row in r:
            if row and not row[0].startswith("EXTERNAL"):
                yield row


def main():
    funcs = {}
    for e, sz, name, *_ in read_tsv("functions.tsv"):
        funcs[int(e, 16)] = (int(sz), name)
    callers = collections.defaultdict(set)
    callees = collections.defaultdict(set)
    for a, b in read_tsv("calls.tsv"):
        if b.startswith("EXTERNAL"):
            continue
        a, b = int(a, 16), int(b, 16)
        callers[b].add(a)
        callees[a].add(b)
    strings = collections.defaultdict(list)
    ts2_seed, eng_seed = set(), set()
    for f, _ins, s in read_tsv("string_refs.tsv"):
        f = int(f, 16)
        strings[f].append(s)
        if TS2_STR.search(s):
            ts2_seed.add(f)
        elif ENGINE_STR.search(s):
            eng_seed.add(f)
    eng_seed -= ts2_seed  # ts2redux\cryengine\... enthält "cry"

    lo_seed, hi_seed = min(ts2_seed), max(ts2_seed)
    eng_sorted = sorted(eng_seed)
    i = bisect.bisect_left(eng_sorted, lo_seed)
    # Die Saat-Spanne darf Engine-Saat enthalten (eingestreute Helfer); die
    # Blockgrenzen sind die nächsten Engine-Funktionen außerhalb.
    block_lo = eng_sorted[i - 1] if i else min(funcs)
    j = bisect.bisect_right(eng_sorted, hi_seed)
    block_hi = eng_sorted[j] if j < len(eng_sorted) else max(funcs) + 1
    inside_eng = [a for a in eng_sorted[i:j]]

    ts2 = {}
    for a in funcs:
        if block_lo < a < block_hi and a not in eng_seed:
            ts2[a] = "Saat" if a in ts2_seed else "im Block"
    for a in ts2_seed:
        ts2.setdefault(a, "Saat außerhalb")
    rounds, changed = 0, True
    while changed:
        changed, rounds = False, rounds + 1
        for g in {c for f in ts2 for c in callees[f]}:
            if g in ts2 or g in eng_seed:
                continue
            cs = callers[g]
            if cs and all(c in ts2 for c in cs):
                ts2[g] = "nur von TS2 gerufen"
                changed = True

    with open(os.path.join(OUT, "ts2_functions.tsv"), "w", encoding="utf-8") as o:
        o.write("entry\tsize\tname\treason\tstrings\n")
        for a in sorted(ts2):
            sz, name = funcs.get(a, (0, ""))
            ss = " ; ".join(x[:60] for x in strings.get(a, [])[:4]).replace("\t", " ")
            o.write(f"{a:x}\t{sz}\t{name}\t{ts2[a]}\t{ss}\n")

    reasons = collections.Counter(ts2.values())
    total = sum(funcs.get(a, (0,))[0] for a in ts2)
    in_block = [a for a in ts2 if block_lo < a < block_hi]
    print(f"Funktionen gesamt: {len(funcs)}")
    print(f"TS2-Block: 0x{block_lo:X} < f < 0x{block_hi:X} "
          f"(letzte/erste Engine-Funktion davor/danach), {len(in_block)} Funktionen")
    print(f"Engine-Saat im Block (ausgenommen): {len(inside_eng)}")
    for a in inside_eng[:20]:
        print(f"  0x{a:X} {strings[a][0][:70]!r}")
    print(f"TS2 gesamt: {len(ts2)} Funktionen, {total / 1024:.0f} KB Code; {dict(reasons)}; {rounds} Runden")
    print(f"mit Strings: {sum(1 for a in ts2 if strings.get(a))}")


if __name__ == "__main__":
    main()
