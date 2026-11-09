#!/usr/bin/env python3
"""Compare base-game vehicle row depth and raw blit against original x86."""

import random
import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_ESI,
    UC_X86_REG_ESP,
)

binary = open("/path/to/user/ra2/game/game.exe", "rb").read()
u = Uc(UC_ARCH_X86, UC_MODE_32)
u.mem_map(0x400000, 0x800000)
u.mem_write(0x400000, binary[:0x431000])
u.mem_map(0x1000000, 0x100000)
u.mem_map(0x2000000, 0x100000)
u.mem_map(0x3000000, 0x1000)
base, stack, stop = 0x1000000, 0x20FFF00, 0x3000000


def w(address, value):
    u.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))


def s(value):
    return struct.unpack("<i", struct.pack("<I", value))[0]


# The interface installed by 0x4FB8D0 supplies these two results.
for offset, expected in ((0x38, 0), (0x3C, 2)):
    target = struct.unpack("<I", u.mem_read(0x7A31EC + offset, 4))[0]
    w(stack, stop)
    w(stack + 4, base)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.emu_start(target, stop, count=100)
    assert u.reg_read(UC_X86_REG_EAX) == expected


w(0x839C6C, base)
w(base + 0x1C, base + 0xFF000)
w(base + 0x20, 0x10000)
w(0x8315B4, base + 0x100)
w(base + 0x100 + 0x1C, base + 0xFF000)
w(base + 0x100 + 0x20, 0x10000)
# Converter tables and both original depth surfaces.
w(base + 0x200 + 4, base + 0x30000)
w(base + 0x200 + 8, base + 0x50000)
u.mem_write(base + 0x30000, bytes(0x20000))
u.mem_write(base + 0x50000, bytes(0x20000))
visible_cases = []
visible_original = []
for bias in (-1, 0, 100, 65535, 65536):
    values = [0, 99, 100, 101, 65535, 101]
    source = bytes([1, 1, 1, 1, 1, 0])
    terrain = struct.pack("<6H", *values)
    u.mem_write(base + 0x1000, b"\x77\x77" * 6)
    u.mem_write(base + 0x2000, source)
    u.mem_write(base + 0x3000, terrain)
    u.mem_write(base + 0x4000, bytes(12))
    args = [base + 0x1000, base + 0x2000, 6, bias, base + 0x3000, base + 0x4000, 0, 0]
    w(stack, stop)
    for i, arg in enumerate(args):
        w(stack + 4 + i * 4, arg)
    u.reg_write(UC_X86_REG_ECX, base + 0x200)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.emu_start(0x488870, stop, count=10000)
    result = struct.unpack("<6H", u.mem_read(base + 0x1000, 12))
    expected = tuple(
        0 if pixel and bias < depth else 0x7777 for pixel, depth in zip(source, values)
    )
    assert result == expected, (bias, result, expected)
    assert bytes(u.mem_read(base + 0x3000, 12)) == terrain
    visible_cases.extend(
        f"{pixel} {bias} {depth}\n" for pixel, depth in zip(source, values)
    )
    visible_original.extend(int(value == 0) for value in result)

visible_native = subprocess.check_output(
    [sys.argv[1], "--depth-visible"], input="".join(visible_cases), text=True
)
assert list(map(int, visible_native.split())) == visible_original

rng = random.Random(4373)
cases = [
    (
        rng.randrange(65536),
        rng.randrange(-1000, 1000),
        rng.randrange(-1000, 1000),
        rng.randrange(0, 50),
        rng.randrange(1, 257),
        rng.randrange(-1000, 1001),
    )
    for _ in range(256)
]
input_text = "".join(" ".join(map(str, case)) + "\n" for case in cases)
native = subprocess.check_output(
    [sys.argv[1], "--depth-rows"], input=input_text, text=True
).splitlines()
flat_input = "".join(
    " ".join(map(str, case[:-1] + (-case[-1] - 1,))) + "\n" for case in cases
)
flat_native = subprocess.check_output(
    [sys.argv[1], "--flat-depth-rows"], input=flat_input, text=True
).splitlines()
assert flat_native == native
for case, line in zip(cases, native):
    zbase, origin, destination, clip, height, adjustment = case
    w(base + 0x24, zbase)
    w(base + 4, origin)
    w(base + 0x800 + 4, destination)
    w(stack + 0x3C, clip)
    w(stack + 0x44, height)
    w(stack + 0x70, adjustment)
    w(stack + 0x74, 0x7CE168 + 2 * 24)
    u.reg_write(UC_X86_REG_ESI, base + 0x800)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.emu_start(0x434C64, 0x434D07, count=1000)
    actual = []
    for _ in range(8):
        actual.extend((s(u.reg_read(UC_X86_REG_EDI)), s(u.reg_read(UC_X86_REG_EBX))))
        u.emu_start(0x434F33, 0x434F4A, count=1000)
    expected = list(map(int, line.split()))
    assert actual == expected, (case, actual, expected)
assert len(native) == len(cases)
print(
    "Base game.exe: locomotor defaults, 256 row gradients and 5 raw blits match original x86"
)
