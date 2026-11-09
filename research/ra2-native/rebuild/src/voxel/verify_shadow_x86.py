#!/usr/bin/env python3
"""Compare one base-game ground shadow with original x86 output via Unicorn."""

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

if len(sys.argv) not in (4, 5):
    raise SystemExit("usage: verify_shadow_x86.py voxel_test model map_facing")
test, name, facing = sys.argv[1], sys.argv[2], int(sys.argv[3])
blocked = len(sys.argv) == 5 and sys.argv[4] == "blocked"
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


def facing_hook(cpu, address, size, unused):
    w(base + 0x5000, facing << 8)
    sp = cpu.reg_read(UC_X86_REG_ESP)
    ret = struct.unpack("<I", cpu.mem_read(sp, 4))[0]
    cpu.reg_write(UC_X86_REG_EAX, base + 0x5000)
    cpu.reg_write(UC_X86_REG_ESP, sp + 8)
    cpu.reg_write(UC_X86_REG_EIP, ret)


u.hook_add(UC_HOOK_CODE, facing_hook, begin=0x4B8BE0, end=0x4B8BE0)
for address in (0x717D70, 0x717D50):
    call(address)
u.reg_write(UC_X86_REG_ESP, stack)
u.emu_start(0x718C7E, 0x718CB4, count=1000000)
call(0x717FB0, args=(struct.unpack("<I", struct.pack("<f", 0.7853749990463257))[0],))
w(base + 0x6000 + 8, base + 0x3000)
call(0x53D030, args=(base + 0x6000, base + 0x700, 0))
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
call(0x7171D0)
surface = 0
if blocked:
    surface = base + 0x8000
    w(surface, base + 0x9000)
    w(base + 0x9000 + 0x28, stop + 0x100)

    def pixel_hook(cpu, address, size, unused):
        sp = cpu.reg_read(UC_X86_REG_ESP)
        ret, point = struct.unpack("<2I", cpu.mem_read(sp, 8))
        x, y = struct.unpack("<2i", cpu.mem_read(point, 8))
        cpu.reg_write(UC_X86_REG_EAX, int((x + y) % 3 == 0))
        cpu.reg_write(UC_X86_REG_ESP, sp + 8)
        cpu.reg_write(UC_X86_REG_EIP, ret)

    u.hook_add(UC_HOOK_CODE, pixel_hook, begin=stop + 0x100, end=stop + 0x100)
call(0x717360, base, 0, (0, 0xAF77A8, base + 0x700, 0x839A60, surface, 1000, 2000))
call(0x7178E0, base + 0xA00)
rx, ry, sx, sy, width, height = struct.unpack("<6i", u.mem_read(base + 0xA00, 24))
buf = bytes(u.mem_read(0xAE3408, 65536))
original = {
    (rx + x, ry + y)
    for y in range(height)
    for x in range(width)
    if 0 <= sx + x < 256 and 0 <= sy + y < 256 and buf[(sy + y) * 256 + sx + x]
}
native_bytes = subprocess.check_output(
    [test, "--shadow-blocked" if blocked else "--shadow-case", name, str(facing)]
)
left, top = struct.unpack_from("<2i", native_bytes)
mask = native_bytes[8:]
native = {(left + i % 256, top + i // 256) for i, value in enumerate(mask) if value}
different = original.symmetric_difference(native)
assert not different, f"{name} facing {facing}: {len(different)} shadow pixels differ"
print(f"Base game.exe: {name} facing {facing} matches {len(original)} shadow pixels")
