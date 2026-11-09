"""Findet RIP-relative Verweise und rel32-Calls auf Adressen (VA oder off:Dateioffset).

Aufruf: python -I xref.py <ziel>...
"""
import sys,os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from hfdis import pe,img,B,func_of
t=[s for s in pe.sections if s.Name.startswith(b'.text')][0]
tv=t.VirtualAddress; tb=np.frombuffer(bytes(img[tv:tv+t.Misc_VirtualSize]),dtype=np.uint8)
def rels():
    n=len(tb)-4
    r=(tb[0:n].astype(np.int64)|(tb[1:n+1].astype(np.int64)<<8)|(tb[2:n+2].astype(np.int64)<<16)|(tb[3:n+3].astype(np.int64)<<24))
    r=np.where(r>=2**31,r-2**32,r)
    return r+np.arange(n)+tv+4
R=None
def xrefs(target_rva):
    global R
    if R is None: R=rels()
    hits=np.nonzero(R==target_rva)[0]
    return [int(h)+tv for h in hits]
def off2rva(off):
    for s in pe.sections:
        if s.PointerToRawData<=off<s.PointerToRawData+s.SizeOfRawData: return off-s.PointerToRawData+s.VirtualAddress
if __name__=='__main__':
    for a in sys.argv[1:]:
        rva=off2rva(int(a[4:],16)) if a.startswith('off:') else int(a,16)-B
        for h in xrefs(rva):
            f=func_of(h); print(a,'xref at',hex(h+B),'func',hex(f[0]+B) if f else None)
