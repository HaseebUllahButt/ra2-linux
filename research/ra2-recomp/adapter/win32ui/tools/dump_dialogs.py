#!/usr/bin/env python3
"""dump_dialogs.py: write every dialog template of game.exe to JSON.
usage: dump_dialogs.py [game.exe] [out.json]
Reads the PE resource tree with no external packages. Handles DLGTEMPLATE and
DLGTEMPLATEEX. Each dialog keeps its raw bytes (hex) so a C loader can be checked
against this file. Also writes a summary of all resource types to stderr."""
import struct, sys, json

EXE = sys.argv[1] if len(sys.argv) > 1 else '/path/to/user/Projects/ra2/ra2-1to1/inputs/game.exe'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'dialogs.json'
d = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + 40 * i
    vs, va, rs, ro = struct.unpack_from('<IIII', d, o + 8)
    secs.append((va, max(vs, rs), ro))

def off(rva):
    for va, sz, ro in secs:
        if va <= rva < va + sz:
            return rva - va + ro
    raise ValueError(hex(rva))

res_rva, res_size = struct.unpack_from('<II', d, pe + 24 + 96 + 2 * 8)
rbase = off(res_rva)
TYPES = {1: 'CURSOR', 2: 'BITMAP', 3: 'ICON', 4: 'MENU', 5: 'DIALOG', 6: 'STRING', 9: 'ACCELERATOR',
         10: 'RCDATA', 12: 'GROUP_CURSOR', 14: 'GROUP_ICON', 16: 'VERSION', 24: 'MANIFEST'}

def rname(v):
    if v & 0x80000000:
        p = rbase + (v & 0x7fffffff)
        n = struct.unpack_from('<H', d, p)[0]
        return d[p + 2:p + 2 + 2 * n].decode('utf-16le')
    return v

def walk(p, path, out):
    _, _, _, _, nn, ni = struct.unpack_from('<IIHHHH', d, p)
    for i in range(nn + ni):
        name, ptr = struct.unpack_from('<II', d, p + 16 + 8 * i)
        if ptr & 0x80000000:
            walk(rbase + (ptr & 0x7fffffff), path + [rname(name)], out)
        else:
            drva, dsz, cp, _ = struct.unpack_from('<IIII', d, rbase + ptr)
            out.append((path + [rname(name)], d[off(drva):off(drva) + dsz], drva))

def sz_or_ord(b, p):
    w = struct.unpack_from('<H', b, p)[0]
    if w == 0xffff:
        return struct.unpack_from('<H', b, p + 2)[0], p + 4
    if w == 0:
        return '', p + 2
    e = p
    while struct.unpack_from('<H', b, e)[0]:
        e += 2
    return b[p:e].decode('utf-16le'), e + 2

def align4(p):
    return (p + 3) & ~3

CLASSES = {0x80: 'BUTTON', 0x81: 'EDIT', 0x82: 'STATIC', 0x83: 'LISTBOX', 0x84: 'SCROLLBAR', 0x85: 'COMBOBOX'}

def parse_dialog(b):
    sig = struct.unpack_from('<HH', b, 0)
    ex = sig == (1, 0xffff)
    r = {'ex': ex}
    if ex:
        _, _, helpid, exstyle, style, n, x, y, cx, cy = struct.unpack_from('<HHIIIHhhhh', b, 0)
        p = 26
        r.update(helpid=helpid)
    else:
        style, exstyle, n, x, y, cx, cy = struct.unpack_from('<IIHhhhh', b, 0)
        p = 18
    r.update(style=style, exstyle=exstyle, x=x, y=y, cx=cx, cy=cy)
    r['menu'], p = sz_or_ord(b, p)
    r['class'], p = sz_or_ord(b, p)
    r['title'], p = sz_or_ord(b, p)
    if style & 0x40:  # DS_SETFONT
        if ex:
            pt, weight, italic, charset = struct.unpack_from('<HHBB', b, p)
            p += 6
            r.update(font_weight=weight, font_italic=italic, font_charset=charset)
        else:
            pt = struct.unpack_from('<H', b, p)[0]
            p += 2
        r['font_size'] = pt
        r['font'], p = sz_or_ord(b, p)
    items = []
    for _ in range(n):
        p = align4(p)
        it = {}
        if ex:
            helpid, exs, st, x, y, cx, cy, cid = struct.unpack_from('<IIIhhhhI', b, p)
            p += 24
            it['helpid'] = helpid
        else:
            st, exs, x, y, cx, cy, cid = struct.unpack_from('<IIhhhhH', b, p)
            p += 18
        it.update(id=cid, style=st, exstyle=exs, x=x, y=y, cx=cx, cy=cy)
        c, p = sz_or_ord(b, p)
        it['class'] = CLASSES.get(c, c) if isinstance(c, int) else c
        it['title'], p = sz_or_ord(b, p)
        extra = struct.unpack_from('<H', b, p)[0]
        p += 2
        if extra:
            it['extra'] = b[p:p + extra].hex()
            p += extra
        items.append(it)
    r['items'] = items
    r['parsed_bytes'] = p
    return r

res = []
walk(rbase, [], res)
summary = {}
for path, data, rva in res:
    t = TYPES.get(path[0], path[0])
    summary[t] = summary.get(t, 0) + 1
dialogs = []
for path, data, rva in res:
    if path[0] != 5:
        continue
    dl = parse_dialog(data)
    dl = {'name': path[1], 'lang': path[2], 'rva': rva, 'size': len(data), **dl, 'raw': data.hex()}
    dialogs.append(dl)
json.dump(dialogs, open(OUT, 'w'), indent=1)
classes = {}
for dl in dialogs:
    for it in dl['items']:
        classes[it['class']] = classes.get(it['class'], 0) + 1
print('resources:', summary, file=sys.stderr)
print('dialogs: %d, controls: %d' % (len(dialogs), sum(len(x['items']) for x in dialogs)), file=sys.stderr)
print('control classes:', classes, file=sys.stderr)
print('dialog classes:', sorted(set(str(x['class']) for x in dialogs)), file=sys.stderr)
