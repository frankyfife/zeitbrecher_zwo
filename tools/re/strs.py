"""Listet ASCII-/UTF-16-Strings einer Datei, die auf einen Regex passen.

Aufruf: python -I strs.py <datei> <regex>
"""
import re,sys
d=open(sys.argv[1],'rb').read()
pat=re.compile(sys.argv[2].encode(),re.I)
seen=set()
for m in re.finditer(rb'[\x20-\x7e]{5,}',d):
    s=m.group()
    if pat.search(s) and s not in seen:
        seen.add(s); print(hex(m.start()),s.decode())
for m in re.finditer(rb'(?:[\x20-\x7e]\x00){5,}',d):
    s=m.group().decode('utf-16le')
    if pat.search(s.encode()) and s not in seen:
        seen.add(s); print(hex(m.start()),'W',s)
