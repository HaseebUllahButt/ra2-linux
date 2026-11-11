#!/usr/bin/env python3
"""Generate exact C ABI slots. Only absent methods get explicit stop bodies."""
import re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
header=Path('/usr/include/wine/windows/ddraw.h').read_text()
source=(root/'src/windows/ddraw.c').read_text()
parts=['/* ABI declarations from Wine ddraw.h. No implementation is copied. */']
for interface,prefix in [('IDirectDraw','draw'),('IDirectDrawSurface','surface'),('IDirectDrawPalette','palette'),('IDirectDrawClipper','clipper')]:
    body=re.search(r'DECLARE_INTERFACE_\('+interface+r',IUnknown\)\s*\{(.*?)\n\};',header,re.S)[1]
    slots=[]
    for m in re.finditer(r'STDMETHOD(?:_\((\w+),\s*(\w+)\)|\((\w+)\))\((.*?)\)\s*PURE;',body,re.S):
        ret=m[1] or 'HRESULT'; name=m[2] or m[3]
        args=' '.join(m[4].split()).replace('THIS_',interface+' *iface,').replace('THIS',interface+' *iface')
        fn=prefix+'_'+name; slots.append('.'+name+'='+fn)
        if not re.search(r'\b'+fn+r'\(',source):
            parts.append('static '+ret+' WINAPI '+fn+'('+args+') { missing("'+interface+'.'+name+'"); return ('+ret+')DDERR_UNSUPPORTED; }')
    parts.append('static '+interface+'Vtbl '+prefix+'_vtable = { '+', '.join(slots)+' };')
(root/'src/windows/ddraw_slots.inc').write_text('\n'.join(parts)+'\n')
