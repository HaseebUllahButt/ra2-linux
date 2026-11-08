#!/usr/bin/env python3
"""Check base 0x462110 standard area-damage call against game.exe.

Build from the repository root:
  g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
    rebuild/src/weapons/{weapons,area_impact_test}.cpp \
    rebuild/src/sim/{combat,target,distance}.cpp -o /tmp/ra2_area_impact
Run with Unicorn installed:
  RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_area_impact_x86.py \
    /tmp/ra2_area_impact
"""

import os
from pathlib import Path
import random
import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import *

u = Uc(UC_ARCH_X86, UC_MODE_32)
u.mem_map(0x400000, 0x800000)
u.mem_write(
    0x400000, (Path(os.environ["RA2_GAME_DIR"]) / "game.exe").read_bytes()[:0x431000]
)
u.mem_map(0x1000000, 0x100000)
u.mem_map(0x2000000, 0x100000)
u.mem_map(0x3000000, 0x1000)
base, stack, stop = 0x1000000, 0x20FFF00, 0x3000000
bullet, attacker, warhead, kind, position = (
    base + n for n in (0x1000, 0x2000, 0x3000, 0x5000, 0x7000)
)


def w(p, v):
    u.mem_write(p, struct.pack("<I", v & 0xFFFFFFFF))


def signed(v):
    return v if v < 0x80000000 else v - 0x100000000


w(bullet + 0x98, kind)
w(bullet + 0x9C, attacker)
w(bullet + 0x110, warhead)
for j, v in enumerate((11, -7, 333)):
    w(position + j * 4, v)
observed = None


def hook(cpu, address, size, user):
    global observed
    sp = cpu.reg_read(UC_X86_REG_ESP)
    coord = cpu.reg_read(UC_X86_REG_ECX)
    xyz = struct.unpack("<iii", cpu.mem_read(coord, 12))
    args = struct.unpack("<III", cpu.mem_read(sp + 4, 12))
    observed = (*xyz, args[0], args[1], signed(cpu.reg_read(UC_X86_REG_EDX)), args[2])
    cpu.emu_stop()


u.hook_add(UC_HOOK_CODE, hook, begin=0x47E2C0, end=0x47E2C0)
rng = random.Random(0x462110)
cases = [
    (rng.randrange(-(2**31), 2**31), rng.randrange(-(2**31), 2**31))
    for _ in range(1024)
]
cases += [
    (damage, multiplier)
    for damage in (-100, -1, 0, 1, 90, 100, 2**31 - 1)
    for multiplier in (-257, -1, 0, 1, 255, 256, 257)
]
lines = subprocess.check_output(
    [sys.argv[1]], input="".join(f"{d} {m}\n" for d, m in cases), text=True
).splitlines()
for (damage, multiplier), line in zip(cases, lines):
    w(bullet + 0x6C, damage)
    w(bullet + 0x138, multiplier)
    w(stack, stop)
    w(stack + 4, position)
    u.reg_write(UC_X86_REG_ECX, bullet)
    u.reg_write(UC_X86_REG_ESP, stack)
    observed = None
    u.emu_start(0x462110, stop, count=10000)
    expected = tuple(map(int, line.split()))
    assert observed == expected, ((damage, multiplier), observed, expected)
assert len(lines) == len(cases)
print(f"Base game.exe: {len(cases)} standard area-impact calls match")
