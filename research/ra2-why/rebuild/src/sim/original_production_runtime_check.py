#!/usr/bin/env python3
"""Compare native production decisions with original base game.exe x86 code.

Runs House::Can_Build 004E3660 and BuildingType::Legal_Placement 0045D880
(006DE0E0, 004718B0, 0055A740) in Unicorn with random resolved inputs.
Only CRT malloc/free are replaced by a bump allocator. No game process runs.
Usage: original_production_runtime_check.py GAME_EXE NATIVE_TEST
"""
import random
import struct
import subprocess
import sys
from original_image import load_original
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP

if len(sys.argv) != 3:
    raise SystemExit('usage: original_production_runtime_check.py GAME_EXE NATIVE_TEST')
cpu = load_original(sys.argv[1])
MEM, SIZE = 0x1000000, 0x400000
cpu.mem_map(MEM, SIZE)
cpu.mem_map(0x3200000, 0x100000)
STACK, STOP = MEM+SIZE-0x1000, MEM+SIZE-0x2000
heap = [0]
def word(a, v): cpu.mem_write(a, struct.pack('<I', v & 0xffffffff))
def byte(a, v): cpu.mem_write(a, bytes([v & 0xff]))
def half(a, v): cpu.mem_write(a, struct.pack('<h', v))
def read(a): return struct.unpack('<I', cpu.mem_read(a, 4))[0]
def hook(uc, address, size, _):
    if address == 0x782017:       # malloc(size)
        esp = uc.reg_read(UC_X86_REG_ESP); n = read(esp+4)
        ptr = MEM+0x300000+heap[0]; heap[0] += (n+15) & ~15
        uc.reg_write(UC_X86_REG_EAX, ptr)
        uc.reg_write(UC_X86_REG_ESP, esp+4); uc.reg_write(UC_X86_REG_EIP, read(esp))
    elif address == 0x781d3b:     # free(ptr)
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_ESP, esp+4); uc.reg_write(UC_X86_REG_EIP, read(esp))
cpu.hook_add(UC_HOOK_CODE, hook, begin=0x781d3b, end=0x781d3b)
cpu.hook_add(UC_HOOK_CODE, hook, begin=0x782017, end=0x782017)
alloc = [0]
def block(n):
    p = MEM+0x10000+alloc[0]; alloc[0] += (n+15) & ~15
    cpu.mem_write(p, b'\0'*n); return p
stubs = {}
def returns(value, pop=0):
    # Stubs live in a separate arena and are never rewritten: Unicorn caches translated code.
    key = (value & 0xffffffff, pop)
    if key not in stubs:
        p = 0x3200000+16*len(stubs)
        cpu.mem_write(p, b'\xb8'+struct.pack('<I', key[0])+(b'\xc2'+struct.pack('<H', pop) if pop else b'\xc3'))
        stubs[key] = p
    return stubs[key]
def vector(address, values):
    data = block(4*max(1, len(values)))
    for i, v in enumerate(values): word(data+4*i, v)
    word(address, 0x79d9d4); word(address+4, data); word(address+8, len(values))
    byte(address+0xc, 0); byte(address+0xd, 0); word(address+0x10, len(values)); word(address+0x14, 10)
def counter(address, values):
    data = block(4*64)
    for i, v in enumerate(values): word(data+4*i, v)
    word(address, 0x7a2f08); word(address+4, data); word(address+8, 64)
def run(entry, ecx, args, limit=200000):
    word(STACK, STOP)
    for i, a in enumerate(args): word(STACK+4+4*i, a)
    cpu.reg_write(UC_X86_REG_ECX, ecx); cpu.reg_write(UC_X86_REG_ESP, STACK)
    cpu.emu_start(entry, STOP, count=limit)
    return cpu.reg_read(UC_X86_REG_EAX)

rng = random.Random(0x4e3660)
RTTI = (7, 0x28, 0x10, 3)
def can_build_case():
    r = rng.random
    t = dict(rtti=rng.choice(RTTI), heap=rng.randrange(8), aa4=int(r() < 0.03),
             prereq=[rng.choice((-6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7)) for _ in range(rng.randrange(4))],
             override=[rng.randrange(8) for _ in range(rng.choice((0, 0, 0, 1, 2)))],
             tech=rng.choice((-1, 0, 1, 3, 5, 10)), b4c=int(r() < 0.1), b4b=int(r() < 0.1),
             req=rng.choice((-1, -1, rng.randrange(1, 64))), forb=rng.choice((-1, -1, rng.randrange(1, 64))),
             sw=rng.choice((-1, -1, 0, 1)), limit=rng.choice((0, 0, -1, -2, 1, 2, 3)), c14=int(r() < 0.3))
    h = dict(tech=rng.choice((0, 1, 5, 10)), s205=int(r() < 0.5), s204=int(r() < 0.5), country=rng.randrange(6),
             masks=[rng.choice((0, 0, rng.randrange(64))) for _ in range(4)], swd=[int(r() < 0.5) for _ in range(2)],
             f134=int(r() < 0.8), f135=int(r() < 0.5),
             active=[rng.choice((0, 0, 1, 2)) for _ in range(8)],
             owned=[[rng.choice((0, 1, 2, 3)) for _ in range(8)] for _ in range(4)],
             built=[[rng.choice((0, 1, 2, 3)) for _ in range(8)] for _ in range(4)],
             buildings=[dict(limbo=int(r() < 0.2), f530=int(r() < 0.8), mission=rng.choice((0, 5, 0x13)),
                             a0=rng.choice((0, 0x13)), up=[rng.choice((-1, -1, rng.randrange(8))) for _ in range(3)])
                        for _ in range(rng.randrange(4))],
             c14=rng.randrange(3), factories=[rng.choice((None, (rng.choice(RTTI), rng.randrange(8)))) for _ in range(rng.randrange(3))])
    g = dict(sw_allowed=int(r() < 0.5), session=rng.choice((0, 1)), groups=[[rng.randrange(8) for _ in range(rng.randrange(3))] for _ in range(6)],
             powers=[int(r() < 0.3) for _ in range(8)], limit_only=int(r() < 0.2), queued=int(r() < 0.5))
    return t, h, g

def serialize(case):
    t, h, g = case
    v = [t['rtti'], t['heap'], t['aa4'], len(t['prereq']), *t['prereq'], len(t['override']), *t['override'],
         t['tech'], t['b4c'], t['b4b'], t['req'], t['forb'], t['sw'], t['limit'], t['c14'],
         h['tech'], h['s205'], h['s204'], h['country'], *h['masks'], *h['swd'], h['f134'], h['f135'], *h['active']]
    for k in range(4): v += h['owned'][k]
    for k in range(4): v += h['built'][k]
    v.append(len(h['buildings']))
    for b in h['buildings']: v += [b['limbo'], b['f530'], b['mission'], b['a0'], *b['up']]
    v += [h['c14'], len(h['factories'])]
    for f in h['factories']: v += [0, 0, 0] if f is None else [1, f[0], f[1]]
    v += [g['sw_allowed'], g['session']]
    for grp in g['groups']: v += [len(grp), *grp]
    v += [*g['powers'], g['limit_only'], g['queued']]
    return ' '.join(map(str, v))+'\n'

def emulate_can_build(case):
    t, h, g = case
    alloc[0] = 0; heap[0] = 0
    types = []
    for i in range(8):           # BuildingType registry for prerequisites.
        p = block(0x1400); types.append(p)
        if g['powers'][i]: byte(p+0xc20, ord('X'))
    registry = block(4*8)
    for i, p in enumerate(types): word(registry+4*i, p)
    word(0xa35cdc, registry); word(0xa35ce8, 8)
    vt = block(0x200)
    word(vt+0x2c, returns(t['rtti'])); word(vt+0x40, returns(t['heap']))
    kind = block(0x1400); word(kind, vt)
    byte(kind+0xaa4, t['aa4']); vector(kind+0x560, t['prereq']); vector(kind+0x57c, t['override'])
    word(kind+0x55c, t['tech']); byte(kind+0xb4c, t['b4c']); byte(kind+0xb4b, t['b4b'])
    word(kind+0xb50, t['req']); word(kind+0xb54, t['forb']); word(kind+0x1374, t['sw'])
    word(kind+0x388, t['limit']); byte(kind+0xc14, t['c14'])
    house = block(0x5700); housetype = block(0x100)
    word(house+0x34, housetype); word(housetype+0xb8, h['country'])
    word(house+0x11c, h['tech']); byte(house+0x205, h['s205']); byte(house+0x204, h['s204'])
    for i, m in enumerate(h['masks']): word(house+0x208+4*i, m)
    sws = block(8)
    for i, d in enumerate(h['swd']):
        sw = block(0x40); swt = block(0x100); word(sw+0x24, swt); byte(swt+0xdf, d); word(sws+4*i, sw)
    word(house+0x1a0, sws)
    byte(house+0x134, h['f134']); byte(house+0x135, h['f135'])
    counter(house+0x5460, h['active'])
    for k, off in enumerate((0x5420, 0x5430, 0x5440, 0x5450)): pass
    order = {7: 0, 0x28: 1, 0x10: 2, 3: 3}
    for k, (owned, built) in enumerate(((0x5420, 0x54a0), (0x5430, 0x54b0), (0x5440, 0x54c0), (0x5450, 0x54d0))):
        counter(house+owned, h['owned'][k]); counter(house+built, h['built'][k])
    blist = block(4*max(1, len(h['buildings'])))
    for i, b in enumerate(h['buildings']):
        bv = block(0x100); obj = block(0x600); word(obj, bv)
        word(bv+0x16c, returns(b['mission'])); byte(obj+0x76, b['limbo']); byte(obj+0x530, b['f530'])
        word(obj+0xa0, b['a0'])
        for j, u in enumerate(b['up']): word(obj+0x4c0+4*j, 0 if u < 0 else types[u])
        word(blist+4*i, obj)
    word(house+0x6c, blist); word(house+0x78, len(h['buildings']))
    inf = block(4*8); n = 0
    for i in range(h['c14']):
        o = block(0x300); word(o+0x1b4, house); word(o+0x290, t['heap']); word(inf+4*n, o); n += 1
    o = block(0x300); word(o+0x1b4, house); word(o+0x290, t['heap']+1); word(inf+4*n, o); n += 1
    word(0x86619c, inf); word(0x8661a8, n)
    facs = block(4*8); m = 0
    other = block(0x5700)
    f = block(0x80); word(f+0x68, other); word(facs+4*m, f); m += 1    # Another house's factory.
    for fo in h['factories']:
        f = block(0x80); word(f+0x68, house)
        if fo is not None:
            ov = block(0x100); obj = block(0x100); word(obj, ov)
            same = fo == (t['rtti'], t['heap'])
            word(ov+0x80, returns(kind if same else block(0x10)))
            word(f+0x54, obj)
        word(facs+4*m, f); m += 1
    word(0xa35e94, facs); word(0xa35ea0, m)
    byte(0xa3d2c3, g['sw_allowed']); word(0xa3d298, g['session'])
    rules = block(0xc00); word(0x839848, rules)
    for i, grp in enumerate(g['groups']): vector(rules+0x2b8+0x1c*i, grp)
    result = run(0x4e3660, house, [kind, g['limit_only'], g['queued']])
    return struct.unpack('<i', struct.pack('<I', result))[0]

def placement_case():
    r = rng.random
    width = rng.randrange(40, 70); lx, ly = rng.randrange(0, 4), rng.randrange(0, 4)
    lw, lh = rng.randrange(20, 40), rng.randrange(20, 40)
    foundation = rng.randrange(22)
    ox, oy = rng.randrange(0, 90), rng.randrange(0, 90)
    if r() < 0.02: ox, oy = 0, 0
    cells = []
    for dx, dy in [(x, y) for y in range(7) for x in range(7)]:
        x, y = ox+dx, oy+dy
        if r() < 0.03: continue        # Unallocated cell: fallback.
        occ = []
        if r() < 0.06: occ.append((rng.choice((1, 6, 0xf)), 1))
        if r() < 0.04: occ.append((2, 1))
        if r() < 0.05: occ.append((0x24, 0))
        if r() < 0.03: occ.append((4, 0))
        cells.append(dict(x=x, y=y, occ=occ, o124=rng.choice((0,)*12+(1, 0x20, 0x40)), height=rng.choice((0, 0, 1, 2, -1)),
                          ramp=rng.choice((0,)*10+(1, 5)), overlay=rng.choice((-1,)*12+(0, 2, 0x1a, 5)),
                          flags=rng.choice((0,)*10+(0x100, 0x400, 0x200)), land=rng.choice((0, 0, 0, 1, 2, 3, 5, 6, 7, 11))))
    build = dict(foundation=foundation, anywhere=int(r() < 0.03))
    return dict(width=width, local=(lx, ly, lw, lh), origin=(ox, oy), cells=cells, build=build,
                buildable=[int(r() < 0.5) for _ in range(12)])

def serialize_placement(c):
    v = [c['width'], *c['local'], *c['origin'], c['build']['foundation'], c['build']['anywhere'], *c['buildable'], len(c['cells'])]
    for cell in c['cells']:
        v += [cell['x'], cell['y'], len(cell['occ'])]
        for o in cell['occ']: v += list(o)
        v += [cell['o124'], cell['height'], cell['ramp'], cell['overlay'], cell['flags'], cell['land']]
    return ' '.join(map(str, v))+'\n'

FALLBACK, MAP = 0xa6f9f8, 0x8324e0
CELLS = MEM+0x200000          # 0x40000 cell pointers.
cpu.mem_map(0x3000000, 0x100000)
CENTER = 0x3300000
cpu.mem_map(CENTER, 0x1000)
# thiscall Center(CoordStruct* out): write 128,128,0 and return out.
cpu.mem_write(CENTER, bytes.fromhex('8b442404c70080000000c7400480000000c7400800000000c20400'))
def fill_cell(p, cell):
    cpu.mem_write(p, b'\0'*0x150)
    half(p+0x24, cell['x']); half(p+0x26, cell['y'])
    head = 0
    for rtti, techno in reversed(cell['occ']):
        vt = block(0x60); word(vt+0x2c, returns(rtti))
        word(vt+0x48, CENTER)   # Center coordinate getter; only its distance ranking is used.
        o = block(0x40); word(o, vt); byte(o+0x14, techno); word(o+0x30, head); head = o
    word(p+0xe4, head); word(p+0xe8, 0)
    byte(p+0x124, cell['o124']); byte(p+0x11b, cell['height']); byte(p+0x11c, cell['ramp'])
    word(p+0x44, cell['overlay']); word(p+0x50, -1); word(p+0x140, cell['flags']); word(p+0xec, cell['land'])
    word(p+0x38, 0xffff)
def emulate_placement(c):
    alloc[0] = 0; heap[0] = 0
    word(MAP+0x138, 0x3000000)   # Replaced below with a sparse table.
    word(MAP+0xf0, c['width'])
    for i, v in enumerate(c['local']): word(MAP+0xf8+4*i, v)
    table = 0x3000000
    cpu.mem_write(table, b'\0'*0x100000)
    used = []
    for cell in c['cells']:
        index = cell['y']*0x200+cell['x']
        p = block(0x150); fill_cell(p, cell); word(table+4*index, p); used.append(index)
    fill_cell(FALLBACK, dict(x=0, y=0, occ=[], o124=0, height=0, ramp=0, overlay=-1, flags=0, land=0))
    for i, b in enumerate(c['buildable']): byte(0x850ec0+0x24*i, b)
    word(0xa40774, 0); byte(0xa40958, 1); byte(0xa40d13, 0)
    byte(0x84ed78, 1)   # 004588C0 static guard already initialized; skip CRT atexit.
    rules = block(0xc00); word(0x839848, rules)
    vt = block(0x200); word(vt+0x2c, returns(7)); word(vt+0x90, 0x4588c0)
    kind = block(0x1400); word(kind, vt)
    word(kind+0xc78, c['build']['foundation']); word(kind+0xb94, 0x84ede8+c['build']['foundation']*120)
    word(kind+0x5a4, -1); byte(kind+0x1387, c['build']['anywhere'])
    origin = block(4); half(origin, c['origin'][0]); half(origin+2, c['origin'][1])
    result = run(0x45d880, kind, [origin, 0]) & 0xff
    for index in used: word(table+4*index, 0)
    return result

# Build the original foundation tables once (00454EF0 static initializer).
word(STACK, STOP); cpu.reg_write(UC_X86_REG_ESP, STACK); cpu.emu_start(0x454ef0, STOP, count=200000)

can = [can_build_case() for _ in range(4000)]
native = subprocess.check_output([sys.argv[2], '--can-build-cases'], text=True,
                                 input=''.join(map(serialize, can))).split()
assert len(native) == len(can), (len(native), len(can))
for i, (case, n) in enumerate(zip(can, native)):
    o = emulate_can_build(case)
    assert int(n) == o, (i, case, n, o)
print('Can_Build 004E3660: %d cases match original x86' % len(can))
places = [placement_case() for _ in range(3000)]
native = subprocess.check_output([sys.argv[2], '--placement-cases'], text=True,
                                 input=''.join(map(serialize_placement, places))).split()
assert len(native) == len(places)
legal = 0
for i, (case, n) in enumerate(zip(places, native)):
    o = emulate_placement(case)
    assert int(n) == o, (i, case, n, o)
    legal += o
print('Legal_Placement 0045D880/006DE0E0/004718B0: %d cases (%d legal) match original x86' % (len(places), legal))
