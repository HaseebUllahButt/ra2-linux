#!/usr/bin/env python3
"""imports.py: list game.exe imports (DLL -> functions) and count call sites per import."""
import struct, sys, re, os, json, collections
sys.path.insert(0, os.path.dirname(__file__))
import lift as L
img = L.Image(L.EXE); d = img.d
pe = struct.unpack_from('<I', d, 0x3c)[0]
rva, size = struct.unpack_from('<II', d, pe + 24 + 96 + 8)
def cstr(va):
    b = img.read(va, 256); return b[:b.index(b'\0')].decode()
out = collections.OrderedDict(); slots = {}
p = img.base + rva
while True:
    olt, ts, fc, name, iat = struct.unpack('<IIIII', img.read(p, 20))
    if not name: break
    dll = cstr(img.base + name).upper(); fns = []
    t = img.base + (olt or iat); s = img.base + iat
    while True:
        e = img.u32(t)
        if not e: break
        fn = ('#%d' % (e & 0xffff)) if e & 0x80000000 else cstr(img.base + e + 2)
        fns.append(fn); slots[s] = (dll, fn); t += 4; s += 4
    out[dll] = fns; p += 20
# call sites: "call dword ptr [slot]" or "jmp dword ptr [slot]" in lifted code
lift_dir = sys.argv[1] if len(sys.argv) > 1 else None
uses = collections.Counter()
if lift_dir:
    for f in os.listdir(lift_dir):
        if f.endswith('.c'):
            for m in re.finditer(r'/\* [0-9a-f]{8} (?:call|jmp) dword ptr \[0x([0-9a-f]+)\]', open(os.path.join(lift_dir, f)).read()):
                a = int(m.group(1), 16)
                if a in slots: uses[slots[a]] += 1
json.dump({k: [[fn, uses[(k, fn)]] for fn in v] for k, v in out.items()}, open('reports/imports.json', 'w'), indent=1)
for k, v in out.items():
    print('%s (%d): %s' % (k, len(v), ', '.join('%s%s' % (fn, '' if uses[(k, fn)] else '(unused)') for fn in v)))
