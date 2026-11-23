#!/usr/bin/env python3
"""Compare a depth-enabled base-game section raster with original x86 output via Unicorn."""

import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_ECX,
    UC_X86_REG_EDX,
    UC_X86_REG_EIP,
    UC_X86_REG_ESP,
)

if len(sys.argv) != 4:
    raise SystemExit("usage: verify_shadow_x86.py voxel_test model direction")
test, name, direction = sys.argv[1], sys.argv[2], int(sys.argv[3])
exe = open("/path/to/user/ra2/game/game.exe", "rb").read()
u = Uc(UC_ARCH_X86, UC_MODE_32)
u.mem_map(0x400000, 0x800000)
u.mem_write(0x400000, exe[:0x431000])
u.mem_map(0x1000000, 0x100000)
u.mem_map(0x2000000, 0x100000)
u.mem_map(0x3000000, 0x1000)
base, stack, stop = 0x1000000, 0x20FFF00, 0x3000000


def w(address, value):
    u.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))


def f(address, value):
    u.mem_write(address, struct.pack("<f", value))


def call(address, ecx=0, edx=0, args=()):
    w(stack, stop)
    for i, value in enumerate(args):
        w(stack + 4 + i * 4, value)
    u.reg_write(UC_X86_REG_ECX, ecx)
    u.reg_write(UC_X86_REG_EDX, edx)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.emu_start(address, stop, count=10000000)


matrix = (
    1,
    -1,
    0,
    0,
    0.5,
    0.5,
    1,
    0,
    -0.2 if direction & 1 else 0.2,
    -0.3 if direction & 2 else 0.3,
    -1 if direction & 4 else 1,
    0,
)
for i, value in enumerate(matrix):
    f(base + 0x700 + 4 * i, value)
b = subprocess.check_output([test, "--asset", name + ".vxl"])
header = 32 + struct.unpack_from("<I", b, 16)[0] * 770
body = header + 28
tail = body + struct.unpack_from("<I", b, 28)[0]
starts, ends, spans = struct.unpack_from("<3I", b, tail)
u.mem_write(base + 0x10000, b[body:tail])
w(base + 0x10, base + 0x100)
w(base + 0x14, base + 0x200)
w(base + 0x100, 0)
for i, value in enumerate((starts, ends, spans)):
    w(base + 0x200 + 4 * i, base + 0x10000 + value)
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
for i, corner in enumerate(corners):
    for j, value in enumerate(corner):
        f(base + 0x240 + i * 12 + j * 4, value)
u.mem_write(0xAE3408, bytes(65536))
u.mem_write(0xAD0A70, bytes(65536))
vpl = subprocess.check_output([test, "--asset", "voxels.vpl"])
u.mem_write(0xAF4608, vpl[784:])
u.mem_write(0xAF8E10, bytes(i % 32 for i in range(256)))
w(0xAF6610, 1)
w(0xAF6614, 1)
call(0x7171D0)
call(0x7174C0, base, 0, (0, base + 0x700))
call(0x7178E0, base + 0xA00)
original = bytes(u.mem_read(0xAE3408, 65536)) + bytes(u.mem_read(0xAD0A70, 65536))
native_bytes = subprocess.check_output([test, "--depth-case", name, str(direction)])
left, top = struct.unpack_from("<2i", native_bytes)
rx, ry, sx, sy, _, _ = struct.unpack("<6i", u.mem_read(base + 0xA00, 24))
assert (left, top) == (rx - sx, ry - sy), ((left, top), (rx - sx, ry - sy))
native = native_bytes[8:]
assert len(native) == len(original)
different = sum(a != b for a, b in zip(native, original))
assert (
    not different
), f"{name} direction {direction}: {different} depth/color bytes differ"
print(f"Base game.exe: {name} direction {direction} depth and color bytes match")
