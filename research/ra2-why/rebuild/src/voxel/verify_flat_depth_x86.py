#!/usr/bin/env python3
"""Run base-game 0x4C9930 with an all-flat synthetic map."""

import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_ECX,
    UC_X86_REG_EIP,
    UC_X86_REG_ESP,
)

b = open("/path/to/user/ra2/game/game.exe", "rb").read()
u = Uc(UC_ARCH_X86, UC_MODE_32)
u.mem_map(0x400000, 0x800000)
u.mem_write(0x400000, b[:0x431000])
u.mem_map(0x1000000, 0x100000)
u.mem_map(0x2000000, 0x100000)
u.mem_map(0x3000000, 0x1000)
base, stack, stop = 0x1000000, 0x20FFF00, 0x3000000
obj, vtable, kind, cell, coords, face = [
    base + n for n in (0x1000, 0x2000, 0x3000, 0x5000, 0x6000, 0x6100)
]


def w(p, x):
    u.mem_write(p, struct.pack("<I", x & 0xFFFFFFFF))


def read(p):
    return struct.unpack("<I", u.mem_read(p, 4))[0]


def ret(cpu, result, nargs=0):
    sp = cpu.reg_read(UC_X86_REG_ESP)
    cpu.reg_write(UC_X86_REG_EAX, result & 0xFFFFFFFF)
    cpu.reg_write(UC_X86_REG_ESP, sp + 4 + nargs * 4)
    cpu.reg_write(UC_X86_REG_EIP, read(sp))


w(obj, vtable)
w(obj + 0x5AC, kind)
w(kind + 0xB60, 17)
w(kind + 0xB64, 29)
w(kind + 0xB5C, 47)
w(kind + 0xB68, 59)
w(obj + 0x564, base + 0x7000)
w(base + 0x7000, 0x7A31EC)
w(vtable + 0x80, 0x3000010)
w(vtable + 0x1A0, 0x3000020)
w(vtable + 0x1B8, 0x5D4800)
w(vtable + 0x2C, 0x3000030)
w(cell + 0x38, 0)
w(cell + 0x44, 0xFFFFFFFF)
u.mem_write(cell + 0x116, b"\xff\xff")
w(0xA40CD4, base + 0x8000)
w(0xA52F00, 0)
w(base + 0x8000, base + 0x9000)
w(base + 0x9000, base + 0xA000)
w(base + 0xA000 + 0x9C, 0x3000040)
w(base + 0xB000, 1)
w(base + 0xB004, 1)
w(base + 0xB008, 60)
w(base + 0xB00C, 30)
w(0x7D8964, 0xF7F)
u.mem_write(0xABE490, struct.pack("<d", 15 / 256))


def hook(cpu, address, size, unused):
    if address == 0x3000010:
        ret(cpu, kind)
    elif address == 0x3000020:
        ret(cpu, coords, 1)
    elif address == 0x3000030:
        ret(cpu, 1)
    elif address == 0x548070:
        ret(cpu, cell, 1)
    elif address == 0x4B8BE0:
        ret(cpu, face, 1)
    elif address == 0x3000040:
        ret(cpu, base + 0xB000)


u.hook_add(UC_HOOK_CODE, hook)
expected_results = []
projected_heights = []
for altitude in (0, 256, 512, 768, 1024):
    for facing in range(0, 65536, 2048):
        w(obj + 0x90, altitude)
        w(face, facing)
        w(stack, stop)
        u.reg_write(UC_X86_REG_ESP, stack)
        u.reg_write(UC_X86_REG_ECX, obj)
        u.emu_start(0x4C9930, stop, count=1000000)
        actual = struct.unpack("<i", struct.pack("<I", u.reg_read(UC_X86_REG_EAX)))[0]
        expected = -(int(altitude * 15 / 256 + (altitude >= 728) + 0.5)) - 1
        assert actual == expected, (altitude, facing, actual, expected)
        projected_heights.append(-expected - 1)
        expected_results.append(actual)
if len(sys.argv) != 2:
    raise SystemExit("usage: verify_flat_depth_x86.py voxel_test")
native = subprocess.check_output(
    [sys.argv[1], "--flat-depth-adjust"],
    input="".join(f"{height}\n" for height in projected_heights),
    text=True,
)
assert list(map(int, native.split())) == expected_results
print("Base game.exe: flat-ground adjustment matches 160 altitude and facing cases")
