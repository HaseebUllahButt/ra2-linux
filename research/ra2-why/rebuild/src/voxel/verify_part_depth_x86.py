#!/usr/bin/env python3
"""Compare clipped flat-part depth against the base game's original caller."""

import random
import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EDX,
    UC_X86_REG_ESP,
)

if len(sys.argv) != 2:
    raise SystemExit("usage: verify_part_depth_x86.py voxel_test")
b = open("/path/to/user/ra2/game/game.exe", "rb").read()
u = Uc(UC_ARCH_X86, UC_MODE_32)
u.mem_map(0x400000, 0x800000)
u.mem_write(0x400000, b[:0x431000])
u.mem_map(0x1000000, 0x200000)
u.mem_map(0x2000000, 0x100000)
u.mem_map(0x3000000, 0x1000)
base, stack, stop = 0x1000000, 0x20FFF00, 0x3000000


def w(p, x):
    u.mem_write(p, struct.pack("<I", x & 0xFFFFFFFF))


def r(p):
    return struct.unpack("<I", u.mem_read(p, 4))[0]


def signed(x):
    return struct.unpack("<i", struct.pack("<I", x))[0]


def rect(p, *a):
    u.mem_write(p, struct.pack("<4i", *a))


dest, src, zsurf, zbuf, conv = [
    base + i for i in (0x1000, 0x2000, 0x3000, 0x4000, 0x5000)
]
for surface, width, height, bpp, buf in (
    (dest, 512, 512, 2, base + 0x60000),
    (src, 256, 256, 1, base + 0x10000),
    (zsurf, 512, 512, 2, base + 0xE0000),
):
    w(surface, 0x79AEF8)
    w(surface + 4, width)
    w(surface + 8, height)
    w(surface + 0x10, bpp)
    w(surface + 0x14, buf)
w(zbuf + 0x14, zsurf)
w(zbuf + 0x18, base + 0xE0000)
w(zbuf + 0x1C, base + 0x160000)
w(zbuf + 0x20, 0x80000)
w(zbuf + 0x24, 0x8000)
w(zbuf + 0x28, 512)
w(0x839C6C, zbuf)
w(0x8315B4, 0)
w(conv, 0x79E20C)
rect(base + 0x6200, 0, 0, 512, 512)
observed = []


def hook(cpu, address, size, unused):
    sp = cpu.reg_read(UC_X86_REG_ESP)
    observed.append(
        (
            signed(r(sp + 0x4C)),
            signed(r(sp + 0x44)),
            signed(cpu.reg_read(UC_X86_REG_EDI)),
            signed(cpu.reg_read(UC_X86_REG_EBX)),
        )
    )
    cpu.emu_stop()


u.hook_add(UC_HOOK_CODE, hook, begin=0x434D07, end=0x434D07)
rng = random.Random(0x434B40)
cases = []
for _ in range(256):
    source_y = rng.randrange(0, 190)
    height = rng.randrange(1, min(66, 257 - source_y))
    top = rng.randrange(-50, 50)
    anchor = rng.randrange(-top - source_y - height + 1, 512 - top - source_y)
    projected = rng.randrange(0, 200)
    cases.append((source_y, height, top, anchor, 512, projected))
    rect(base + 0x6000, 97, source_y, 63, height)
    w(base + 0x6100, 200)
    w(base + 0x6104, anchor + top + source_y)
    args = [
        src,
        base + 0x6000,
        base + 0x6100,
        base + 0x6200,
        0,
        conv,
        -projected - 1,
        2,
        0,
        0,
    ]
    w(stack, stop)
    for i, arg in enumerate(args):
        w(stack + 4 + 4 * i, arg)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_ECX, dest)
    u.reg_write(UC_X86_REG_EDX, conv)
    u.emu_start(0x49F3A0, stop, count=100000)
assert len(observed) == len(cases)
text = "".join(" ".join(map(str, c)) + "\n" for c in cases)
native = subprocess.check_output(
    [sys.argv[1], "--flat-part-depth"], input=text, text=True
)
actual = [tuple(map(int, line.split())) for line in native.splitlines()]
for case, original, result in zip(cases, observed, actual):
    assert original == result, (case, original, result)
assert len(actual) == len(cases)
print("Base game.exe: 256 clipped flat-part row inputs match the original caller")
