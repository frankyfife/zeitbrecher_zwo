"""Trennt TS2-Funktionen von CRYENGINE-/Homefront-Funktionen.

Eingabe: der Ghidra-Export in re/out/ (functions.tsv, calls.tsv,
string_refs.tsv, von re/ghidra_scripts/ExportFunctions.java).
Ausgabe: re/out/ts2_functions.tsv (entry, size, Grund) und eine Übersicht
auf stdout.

Vorgehen:
 1. Saat TS2: Funktionen, die TS2-typische Strings nutzen (Quellpfade
    Code\\TS2\\ts2redux, Datenpfade ob/ host0: .xbr .pob, Texte mit
    "TimeSplitters" ...).
 2. Saat Engine: Funktionen mit anderen __FILE__-Pfaden (d:\\work\\dsdb\\...)
    oder typischen CRYENGINE-Strings.
 3. Ausbreiten: eine Funktion ohne Saat gehört zu TS2, wenn alle ihre Aufrufer
    zu TS2 gehören (sie wird nur von TS2 benutzt). Bis nichts mehr dazukommt.
Funktionen, die TS2 und Engine gemeinsam nutzen (CRT, Mathe, Container),
bleiben "geteilt".

Aufruf: python -I classify.py [re/out]
"""
import collections
import csv
import os
import re
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "re", "out")

TS2_STR = re.compile(
    r"(?i)code\\ts2\\|ts2redux|^host0:|^ob/|\.xbr$|\.pob$|timesplitter|^ts2/|"
    r"arcade (league|custom|awards|series|level|match)|mapmaker|"
    r"^\s*(story|challenge) mode|frd|free radical")
ENGINE_FILE = re.compile(r"(?i)^d:\\work\\dsdb\\(?!code\\ts2\\)")
ENGINE_STR = re.compile(r"(?i)^(cry|ientity|i?scriptbind|flowgraph|cvar|g_|r_|s_|e_|sys_|p_|ca_|ai_)|\bcry[a-z]+::")


def read_tsv(name):
    with open(os.path.join(OUT, name), encoding="utf-8", errors="replace", newline="") as f:
        r = csv.reader(f, delimiter="\t", quoting=csv.QUOTE_NONE)
        next(r)
        for row in r:
            yield row


def main():
    size = {}
    for row in read_tsv("functions.tsv"):
        size[row[0]] = int(row[1])
    callers = collections.defaultdict(set)
    callees = collections.defaultdict(set)
    for a, b in read_tsv("calls.tsv"):
        callers[b].add(a)
        callees[a].add(b)
    ts2, eng = {}, {}
    for f, _ins, s in read_tsv("string_refs.tsv"):
        if TS2_STR.search(s):
            ts2.setdefault(f, "string: " + s[:60])
        elif ENGINE_FILE.search(s) or ENGINE_STR.search(s):
            eng.setdefault(f, "string: " + s[:60])
    for f in list(ts2):
        if f in eng and "code\\ts2" not in ts2[f].lower():
            # beide Arten von Strings: als geteilt behandeln
            del ts2[f]
    seeds = len(ts2)
    changed = True
    rounds = 0
    while changed:
        changed = False
        rounds += 1
        frontier = set()
        for f in ts2:
            frontier |= callees[f]
        for g in frontier:
            if g in ts2 or g in eng:
                continue
            cs = callers[g]
            if cs and all(c in ts2 for c in cs):
                ts2[g] = "nur von TS2 gerufen"
                changed = True
    total = sum(size.get(f, 0) for f in ts2)
    with open(os.path.join(OUT, "ts2_functions.tsv"), "w", encoding="utf-8") as o:
        o.write("entry\tsize\treason\n")
        for f in sorted(ts2):
            o.write(f"{f}\t{size.get(f, 0)}\t{ts2[f]}\n")
    print(f"Funktionen gesamt: {len(size)}")
    print(f"TS2: {len(ts2)} ({seeds} Saat, {len(ts2) - seeds} durch Ausbreitung, {rounds} Runden), {total / 1024:.0f} KB Code")
    print(f"Engine-Saat: {len(eng)}")
    hist = collections.Counter(int(f, 16) >> 20 for f in ts2)
    print("Verteilung (MB-Blöcke):")
    for k in sorted(hist):
        print(f"  0x{k << 20:09X}: {hist[k]}")


if __name__ == "__main__":
    main()
