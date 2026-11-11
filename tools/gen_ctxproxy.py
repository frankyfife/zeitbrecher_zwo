"""Erzeugt src/host/ctxproxy.inc: Weiterleitungen aller Methoden von
ID3D11DeviceContext (aus d3d11.h des Windows SDK) für die Untersuchungs-
Hülle in src/host/ctxproxy.h.

Aufruf: python -I tools/gen_ctxproxy.py <d3d11.h> src/host/ctxproxy.inc

Die D3D11-Laufzeit schreibt die Funktionstabelle des Kontexts selbst um;
eine umgelenkte vtable hält deshalb nicht. Die Hülle bekommt TS2 statt des
echten Kontexts. Einzelne Methoden (Hooks) überschreibt ctxproxy.h.
"""
import re
import sys

HOOKED = {'OMSetBlendState', 'DrawIndexed', 'Draw', 'ClearRenderTargetView',
          # Supersampling (src/host/supersample.h)
          'OMSetRenderTargets', 'OMSetRenderTargetsAndUnorderedAccessViews', 'RSSetViewports',
          'RSGetViewports', 'RSSetScissorRects', 'RSGetScissorRects', 'CopySubresourceRegion',
          'CopyResource', 'UpdateSubresource', 'GetData', 'ClearState'}


def split_top(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur)
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return out


def main():
    src = open(sys.argv[1], encoding='utf-8', errors='replace').read()
    start = src.index('ID3D11DeviceContext : public ID3D11DeviceChild')
    body = src[start:src.index('};', start)]
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    out = ['// Erzeugt von tools/gen_ctxproxy.py aus d3d11.h, nicht von Hand ändern.\n']
    for m in re.finditer(r'virtual\s+(.*?)\s+STDMETHODCALLTYPE\s+(\w+)\s*\((.*?)\)\s*=\s*0\s*;', body, re.S):
        ret, name, params = m.group(1).strip(), m.group(2), m.group(3).strip()
        args = []
        if params and params != 'void':
            for p in split_top(params):
                ids = re.findall(r'[A-Za-z_]\w*', re.sub(r'_\w+_\s*\(.*?\)\s*$', '', p.strip()))
                ids = re.findall(r'[A-Za-z_]\w*', p)
                args.append(ids[-1])
        call = f'real_->{name}({", ".join(args)})'
        hook = name in HOOKED
        decl = ' '.join(params.split())
        fn = f'{name}_real' if hook else name
        line = f'{ret} STDMETHODCALLTYPE {fn}({decl})' + ('' if hook else ' override')
        line += f' {{ {"" if ret == "void" else "return "}{call}; }}\n'
        out.append(line)
    open(sys.argv[2], 'w', encoding='utf-8', newline='\n').write(''.join(out))
    print(len(out) - 1, 'Methoden')


if __name__ == '__main__':
    main()
