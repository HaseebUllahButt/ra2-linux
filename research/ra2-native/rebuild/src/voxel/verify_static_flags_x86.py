#!/usr/bin/env python3
"""Compare base-game bridge-gap and ShadowCaster defaults with original x86."""

import struct
import subprocess
import sys
import re
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EIP,
    UC_X86_REG_ESP,
)

if len(sys.argv) != 2:
    raise SystemExit("usage: verify_static_flags_x86.py voxel_test")
b = open("/path/to/user/ra2/game/game.exe", "rb").read()
u = Uc(UC_ARCH_X86, UC_MODE_32)
u.mem_map(0x400000, 0x800000)
u.mem_write(0x400000, b[:0x431000])
u.mem_map(0x1000000, 0x800000)
u.mem_map(0x2000000, 0x100000)
u.mem_map(0x3000000, 0x1000)
base, stack, stop = 0x1000000, 0x20FFF00, 0x3000000
world, record, grid, cells = (
    base + 0x1000,
    base + 0x2000,
    base + 0x100000,
    base + 0x400000,
)


def w(p, x):
    u.mem_write(p, struct.pack("<I", x & 0xFFFFFFFF))


def r(p):
    return struct.unpack("<I", u.mem_read(p, 4))[0]


def call(address, ecx=0):
    w(stack, stop)
    u.reg_write(UC_X86_REG_ECX, ecx)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.emu_start(address, stop, count=1000000)


call(0x491620)  # Original eight cell directions.
w(0x832618, grid)
w(world + 0x50, record)
w(world + 0x5C, 1)
for y in range(32):
    for x in range(32):
        ptr = cells + (y * 32 + x) * 0x200
        w(grid + (y * 512 + x) * 4, ptr)
        w(ptr + 0x24, x | y << 16)

cases = []
for horizontal in (False, True):
    for reverse in (False, True):
        for state, kind in ((0, 0), (1, 0), (0, 1)):
            for mask in range(7):
                start = (10, 12)
                end = (10, 19) if not horizontal else (17, 12)
                if reverse:
                    start, end = end, start
                cases.append((*start, *end, state, kind, mask))
input_text = "".join(" ".join(map(str, c)) + "\n" for c in cases)
native = subprocess.check_output(
    [sys.argv[1], "--bridge-gap-case"], input=input_text, text=True
).splitlines()
for case, expected in zip(cases, native):
    x1, y1, x2, y2, state, kind, mask = case
    u.mem_write(record, struct.pack("<hhhhBxxxI", x1, y1, x2, y2, state, kind))
    for y in range(32):
        for x in range(32):
            flags = (0x100 if (x + y) % 7 == mask else 0) | (
                0x400 if (x * 3 + y) % 11 == 0 else 0
            )
            w(cells + (y * 32 + x) * 0x200 + 0x140, flags)
    call(0x568B00, world)
    actual = "".join(
        "1" if r(cells + (y * 32 + x) * 0x200 + 0x140) & 0x400 else "0"
        for y in range(32)
        for x in range(32)
    )
    assert actual == expected, case
assert len(native) == len(cases)

# Execute the original map flag pass, including its default table constructor.
call(0x470630)
call(0x528540)
starts = (1000, 1100, 1200, 1300, 1400)
for i, start in enumerate(starts):
    w(0xA52E74 + 4 * i, start)
type_object = base + 0x4000
w(base + 0x5000, 10 | (10 << 16))


def hook(cpu, address, size, unused):
    if address == 0x78298A:
        sp = cpu.reg_read(UC_X86_REG_ESP)
        cpu.reg_write(UC_X86_REG_EIP, r(sp))
        cpu.reg_write(UC_X86_REG_ESP, sp + 4)
    elif address == 0x548070:
        sp = cpu.reg_read(UC_X86_REG_ESP)
        xy = r(r(sp + 4))
        x = xy & 0xFFFF
        y = xy >> 16
        assert x < 32 and y < 32, (x, y)
        cpu.reg_write(UC_X86_REG_EAX, cells + (y * 32 + x) * 0x200)
        cpu.reg_write(UC_X86_REG_EIP, r(sp))
        cpu.reg_write(UC_X86_REG_ESP, sp + 8)


u.hook_add(UC_HOOK_CODE, hook)
shadow_cases = (
    [(tile, *starts, 1) for tile in range(995, 1045)]
    + [(1026, *starts, 0)]
    + [(1126, *starts, 1), (1026, 1005, 1000, 1200, 1300, 1400, 1)]
)
shadow_input = "".join(" ".join(map(str, c)) + "\n" for c in shadow_cases)
shadow_native = subprocess.check_output(
    [sys.argv[1], "--shadow-marker-case"], input=shadow_input, text=True
).splitlines()
for case, expected in zip(shadow_cases, shadow_native):
    tile_id, *case_starts, enabled = case
    for i, start in enumerate(case_starts):
        w(0xA52E74 + 4 * i, start)
    u.mem_write(type_object + 0x2B1, bytes([enabled]))
    w(type_object + 0x264, tile_id)
    for y in range(32):
        for x in range(32):
            w(cells + (y * 32 + x) * 0x200 + 0x140, 0)
    w(stack, stop)
    u.reg_write(UC_X86_REG_EBP, type_object)
    u.reg_write(UC_X86_REG_EDI, base + 0x5000)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.emu_start(0x472D20, 0x472DE7, count=1000000)
    assert u.reg_read(UC_X86_REG_EIP) == 0x472DE7
    actual = "".join(
        "1" if r(cells + (y * 32 + x) * 0x200 + 0x140) & 0x10000 else "0"
        for y in range(32)
        for x in range(32)
    )
    assert actual == expected, case
assert len(shadow_native) == len(shadow_cases)
for name in ("temperat.ini", "snow.ini", "urban.ini"):
    data = subprocess.check_output([sys.argv[1], "--asset", name])
    assert not re.search(rb"(?im)^[ \t]*shadowcaster[ \t]*=", data), name
print(
    f"Base game.exe: {len(cases)} bridge gap cases and {len(shadow_cases)} ShadowCaster cases match"
)
print(
    "Base game assets: TEMPERAT.INI, SNOW.INI and URBAN.INI use the default ShadowCaster=false"
)
