#!/usr/bin/env python3
"""Compare a complete flat HTNK with base-game x86 drawing.

The test executable supplies original assets and its RGBA output. Unicorn runs
original caller, matrix, lighting, and raster code. No image is written.
"""

import struct, subprocess, sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EIP
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP

exe = open("/path/to/user/ra2/game/game.exe", "rb").read()
u = Uc(UC_ARCH_X86, UC_MODE_32)
u.mem_map(0x400000, 0x800000)
u.mem_write(0x400000, exe[:0x431000])
u.mem_map(0x1000000, 0x100000)
u.mem_map(0x2000000, 0x100000)
u.mem_map(0x3000000, 0x1000)
base = 0x1000000
stack = 0x20FFF00
stop = 0x3000000
if len(sys.argv) < 2:
    raise SystemExit("usage: verify_vehicle_x86.py voxel_test [0|16 [model [hull_facing [turret_facing]]]]")
test = sys.argv[1]
turret_offset = int(sys.argv[2]) if len(sys.argv) > 2 else 16
model = sys.argv[3] if len(sys.argv) > 3 else "htnk"
facing = int(sys.argv[4]) if len(sys.argv) > 4 else 64
turret_facing = int(sys.argv[5]) if len(sys.argv) > 5 else facing
if not (0 <= facing <= 255):
    raise SystemExit("invalid facing")
if not (0 <= turret_facing <= 255):
    raise SystemExit("invalid turret facing")
if turret_offset not in (0, 16):
    raise SystemExit("offset must be 0 or 16")


def w(p, x):
    u.mem_write(p, struct.pack("<I", x & 0xFFFFFFFF))


def f(p, x):
    u.mem_write(p, struct.pack("<f", x))


def asset(n):
    return subprocess.check_output([test, "--asset", n])


def call(addr, ecx=0, edx=0, args=()):
    w(stack, stop)
    for i, x in enumerate(args):
        w(stack + 4 + i * 4, x)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_ECX, ecx)
    u.reg_write(UC_X86_REG_EDX, edx)
    u.emu_start(addr, stop, count=10000000)


for a in [0x717D70, 0x717D50]:
    call(a)
u.reg_write(UC_X86_REG_ESP, stack)
u.emu_start(0x718C7E, 0x718CB4, count=1000000)
word = struct.unpack("<I", struct.pack("<f", 0.7853749990463257))[0]
call(0x717FB0, args=(word,))
ident = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0)
for i, x in enumerate(ident):
    f(0x839A70 + i * 4, x)
vpl = asset("voxels.vpl")
u.mem_write(0xAF4608, vpl[784:])
w(0xAF6614, 1)
w(0xAF6610, 0)
# Execute the original flat vehicle caller to obtain turret and barrel matrices.
obj, kind, faceptr = base + 0x3000, base + 0x4000, base + 0x5000
w(obj + 0x418, kind)
w(kind + 0xB8, 1)
w(kind + 0x63C, turret_offset)
for i, value in enumerate(ident):
    f(stack + 0x58 + 4 * i, value)


phase_facing = turret_facing


def face_hook(cpu, address, size, unused):
    w(faceptr, 0x4000 if cpu.reg_read(UC_X86_REG_ECX) == obj + 0x2C8 else phase_facing << 8)
    sp = cpu.reg_read(UC_X86_REG_ESP)
    ret = struct.unpack("<I", cpu.mem_read(sp, 4))[0]
    cpu.reg_write(UC_X86_REG_EAX, faceptr)
    cpu.reg_write(UC_X86_REG_ESP, sp + 8)
    cpu.reg_write(UC_X86_REG_EIP, ret)


u.hook_add(UC_HOOK_CODE, face_hook, begin=0x4B8BE0, end=0x4B8BE0)
u.reg_write(UC_X86_REG_EBP, obj)
u.reg_write(UC_X86_REG_ESP, stack)
u.emu_start(0x43AF89, 0x43B10E, count=1000000)
turret = struct.unpack("<12f", u.mem_read(stack + 0x58, 48))
barrel = struct.unpack("<12f", u.mem_read(stack + 0x88, 48))
# The base locomotion matrix reads the hull facing separately.
phase_facing = facing
w(base + 0x6000 + 8, obj)
call(0x53D030, args=(base + 0x6000, base + 0x6100, 0))
hull = struct.unpack("<12f", u.mem_read(base + 0x6100, 48))
locals_by_name = {model: hull, model + "tur": turret, model + "barl": barrel}

screen = {}
parts_data = subprocess.check_output(
    [test, "--vehicle-parts-case", model, str(facing), str(turret_offset), str(turret_facing)]
)
assert struct.unpack_from("<I", parts_data)[0] == 3
part_offset = 4
quadrant = (((turret_facing << 8) >> 13) + 1) >> 1 & 3
part_names = (
    [model, model + "barl", model + "tur"]
    if quadrant in (0, 3)
    else [model, model + "tur", model + "barl"]
)
for name in part_names:
    b = asset(name + ".vxl")
    header = 32 + struct.unpack_from("<I", b, 16)[0] * 770
    body = header + 28
    tail = body + struct.unpack_from("<I", b, 28)[0]
    starts, ends, span = struct.unpack_from("<3I", b, tail)
    u.mem_write(base + 0x10000, b[body:tail])
    w(base + 0x10, base + 0x100)
    w(base + 0x14, base + 0x200)
    w(base + 0x100, 0)
    for j, z in enumerate((starts, ends, span)):
        w(base + 0x200 + j * 4, base + 0x10000 + z)
    u.mem_write(base + 0x2A0, b[tail + 88 : tail + 92])
    mn = struct.unpack_from("<3f", b, tail + 64)
    mx = struct.unpack_from("<3f", b, tail + 76)
    corners = [
        (mx[0], mx[1], mn[2]),
        (mx[0], mn[1], mn[2]),
        (mn[0], mn[1], mn[2]),
        (mn[0], mx[1], mn[2]),
        (mx[0], mx[1], mx[2]),
        (mx[0], mn[1], mx[2]),
        (mn[0], mn[1], mx[2]),
        (mn[0], mx[1], mx[2]),
    ]
    for i, c in enumerate(corners):
        for j, z in enumerate(c):
            f(base + 0x240 + i * 12 + j * 4, z)
    local = locals_by_name[name]
    for i, z in enumerate(local):
        f(base + 0x700 + i * 4, z)
    call(0x591180, base + 0x800, 0xAF77A8, (base + 0x700,))
    u.mem_write(0xAE3408, bytes(65536))
    call(
        0x7170D0,
        base,
        0,
        (
            0,
            base + 0x800,
            0x839A70,
            0x839AB0,
            struct.unpack("<I", struct.pack("<f", 3))[0],
        ),
    )
    call(0x7171D0)
    # The original loader scales HVA translations by the first section scale.
    animation = asset(name + ".hva")
    assert struct.unpack_from("<II", animation, 16) == (1, 1)
    motion = list(struct.unpack_from("<12f", animation, 40))
    scale = struct.unpack_from("<f", b, tail + 12)[0]
    for index in (3, 7, 11):
        motion[index] *= scale
    for i, value in enumerate(motion):
        f(base + 0x900 + i * 4, value)
    call(0x591180, base + 0x950, base + 0x800, (base + 0x900,))
    call(0x7174C0, base, 0, (0, base + 0x950))
    call(0x7178E0, base + 0xA00)
    rx, ry, sx, sy, ww, hh = struct.unpack("<6i", u.mem_read(base + 0xA00, 24))
    buf = bytes(u.mem_read(0xAE3408, 65536))
    left, top, nx, ny, nw, nh = struct.unpack_from("<6i", parts_data, part_offset)
    part_offset += 24
    pixels = parts_data[part_offset : part_offset + 65536 * 4]
    part_offset += 65536 * 4
    assert (left + nx, top + ny, nx, ny, nw, nh) == (rx, ry, sx, sy, ww, hh), (
        name,
        (left + nx, top + ny, nx, ny, nw, nh),
        (rx, ry, sx, sy, ww, hh),
    )
    for y in range(sy, sy + hh):
        for x in range(sx, sx + ww):
            index = buf[y * 256 + x]
            expected = (
                (tuple(vpl[16 + index * 3 : 19 + index * 3]) + (255,))
                if index
                else (0, 0, 0, 0)
            )
            offset = (y * 256 + x) * 4
            actual = tuple(pixels[offset : offset + 4])
            assert actual == expected, (name, x, y, actual, expected)
    print(name, (rx, ry, sx, sy, ww, hh), "nonzero", sum(x != 0 for x in buf))
    for y in range(hh):
        for x in range(ww):
            ix, iy = sx + x, sy + y
            if 0 <= ix < 256 and 0 <= iy < 256:
                color = buf[iy * 256 + ix]
                if color:
                    screen[(rx + x, ry + y)] = color
p = subprocess.check_output(
    [test, "--vehicle-case", model, str(facing), str(turret_offset), str(turret_facing)]
)
left, top = struct.unpack_from("<2i", p)
pix = p[8:]
native = {}
for i in range(65536):
    rgba = pix[i * 4 : i * 4 + 4]
    if rgba[3]:
        native[(left + i % 256, top + i // 256)] = tuple(rgba)
original = {
    xy: tuple(vpl[16 + index * 3 : 19 + index * 3]) + (255,)
    for xy, index in screen.items()
}
allxy = set(original) | set(native)
mism = [xy for xy in allxy if original.get(xy) != native.get(xy)]
assert not mism, f"{len(mism)} visible pixels differ"
description = f"{model} facing {facing}"
if turret_facing != facing:
    description += f" turret {turret_facing}"
print(f"Base game.exe: {description} offset {turret_offset} matches {len(original)} visible RGBA pixels")
