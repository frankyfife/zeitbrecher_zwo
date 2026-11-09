"""Disassembliert die Funktion (laut .pdata) um eine Adresse.

Aufruf: python -I hfdis.py <VA | off:Dateioffset>
"""
import pefile,sys,struct,bisect,re
from capstone import *
import os
# Analysiertes Programm: HF2_EXE oder die Referenzkopie im Repo.
P=os.environ.get("HF2_EXE") or os.path.join(os.path.dirname(os.path.abspath(__file__)),"..","..","reference","homefront","Bin64","Homefront2_Release.exe")
pe=pefile.PE(P,fast_load=True)
img=pe.get_memory_mapped_image()
B=pe.OPTIONAL_HEADER.ImageBase
# pdata
pd=[s for s in pe.sections if s.Name.startswith(b'.pdata')][0]
raw=img[pd.VirtualAddress:pd.VirtualAddress+pd.Misc_VirtualSize]
funcs=[struct.unpack_from('<III',raw,i) for i in range(0,len(raw)-11,12)]
funcs=[f for f in funcs if f[0]]
funcs.sort()
starts=[f[0] for f in funcs]
def func_of(rva):
    i=bisect.bisect_right(starts,rva)-1
    return funcs[i] if i>=0 else None
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=False
def dis(start,end,mark=None):
    for ins in md.disasm(bytes(img[start:end]),B+start):
        m='>>' if mark and ins.address-B<=mark<ins.address-B+ins.size else '  '
        ops=ins.op_str
        # annotate rip-relative strings
        mm=re.search(r'\[rip ([+-]) (0x[0-9a-f]+)\]',ops)
        note=''
        if mm:
            t=ins.address+ins.size+(int(mm.group(2),16)*(1 if mm.group(1)=='+' else -1))-B
            if 0<t<len(img):
                s=bytes(img[t:t+64]).split(b'\0')[0]
                if len(s)>=4 and all(32<=c<127 for c in s): note=' ; "%s"'%s.decode()
                note=' ; ->%x'%(t+B)+note
        print(m,hex(ins.address),ins.mnemonic,ops,note)
if __name__=='__main__':
    a=sys.argv[1]
    if a.startswith('off:'): rva=int(a[4:],16)+0xc00
    else: rva=int(a,16)-B if int(a,16)>B else int(a,16)
    f=func_of(rva)
    print('func',hex(B+f[0]),hex(B+f[1]))
    dis(f[0],f[1],rva)
