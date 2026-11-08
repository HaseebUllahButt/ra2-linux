#!/usr/bin/env python3
"""Compare the complete stable-map base terrain trace with game.exe."""

import os
from pathlib import Path
import random
import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import *

game_directory = Path(os.environ.get("RA2_GAME_DIR", Path(__file__).resolve().parents[3] / "game")).resolve()
os.environ.setdefault("RA2_GAME_DIR", str(game_directory))

u = Uc(UC_ARCH_X86, UC_MODE_32)
u.mem_map(0x400000, 0x800000)
u.mem_write(0x400000, (game_directory / "game.exe").read_bytes()[:0x431000])
u.mem_map(0x1000000, 0x100000)
u.mem_map(0x2000000, 0x100000)
u.mem_map(0x3000000, 0x1000)
base, stack, stop = 0x1000000, 0x20FFF00, 0x3000000
source, dest, sample, ref = (base + n for n in (0x1000, 0x3000, 0x5000, 0x7000))
projectile, rules, overlay, table, owners, vtable = (
    base + n for n in (0x9000, 0xB000, 0xD000, 0xF000, 0x11000, 0x13000)
)


def w(p, v):
    u.mem_write(p, struct.pack("<I", v & 0xFFFFFFFF))


def ret(cpu, value, count):
    sp = cpu.reg_read(UC_X86_REG_ESP)
    cpu.reg_write(UC_X86_REG_EAX, value & 0xFFFFFFFF)
    cpu.reg_write(UC_X86_REG_EIP, struct.unpack("<I", cpu.mem_read(sp, 4))[0])
    cpu.reg_write(UC_X86_REG_ESP, sp + 4 + count * 4)


coords = {}
heights = {}
allied = 0
lookup_coords = []


def hook(cpu, address, size, user):
    if address == 0x548070:
        sp = cpu.reg_read(UC_X86_REG_ESP)
        ptr = struct.unpack("<I", cpu.mem_read(sp + 4, 4))[0]
        lookup_coords.append(struct.unpack("<hh", cpu.mem_read(ptr, 4)))
        ret(cpu, sample, 1)
    elif address == stop + 0x100:
        sp = cpu.reg_read(UC_X86_REG_ESP)
        ptr = struct.unpack("<I", cpu.mem_read(sp + 4, 4))[0]
        xyz = coords[cpu.reg_read(UC_X86_REG_ECX)]
        cpu.mem_write(ptr, struct.pack("<iii", *xyz))
        ret(cpu, ptr, 1)
    elif address == 0x4E5540:
        ret(cpu, allied, 1)


for address in (0x548070, stop + 0x100, 0x4E5540):
    u.hook_add(UC_HOOK_CODE, hook, begin=address, end=address)
for p in (source, dest, sample, ref):
    w(p, vtable)
w(vtable + 0x48, stop + 0x100)
w(0xA35DEC, table)
w(table, overlay)
w(0x839848, rules)
w(0xA3229C, owners)
rng = random.Random(0x4BBAF0)
cases = []
for i in range(4096):
    cliffs, walls, same, wall, rule, owner, allied_case = (
        rng.randrange(2) for _ in range(7)
    )
    hr = rng.randrange(-8, 9)
    ls, ld = rng.randrange(-4, 5), rng.randrange(-4, 5)
    hsrc = ls + 4 * rng.randrange(2)
    sample_level = ld if same else rng.randrange(-4, 5)
    hs = sample_level + 4 * rng.randrange(2)
    xyz = tuple(rng.randrange(-1024, 1025) for _ in range(3))
    cell = tuple(rng.randrange(-1024, 1025) for _ in range(3))
    srcxyz = tuple(rng.randrange(-1024, 1025) for _ in range(3))
    dstxyz = tuple(rng.randrange(-1024, 1025) for _ in range(3))
    cases.append(
        (
            cliffs,
            walls,
            hs,
            hr,
            hsrc,
            same,
            wall,
            ls,
            ld,
            rule,
            owner,
            allied_case,
            xyz,
            cell,
            srcxyz,
            dstxyz,
        )
    )

# Cover the distance boundary and 16-bit cell-coordinate wrap.
for delta in (0, 1, 84, 85, 86, 87, 128, 512):
    cases.append(
        (
            1,
            1,
            5,
            0,
            0,
            0,
            1,
            0,
            0,
            0,
            0,
            0,
            (delta, 0, 0),
            (0, 0, 0),
            (0, 0, 0),
            (1000, 2000, 0),
        )
    )
for value in (
    -2147483648,
    -8388864,
    -257,
    -256,
    -255,
    -1,
    0,
    1,
    255,
    256,
    257,
    8388608,
    2147483647,
):
    cases.append(
        (
            1,
            1,
            5,
            0,
            0,
            0,
            1,
            0,
            0,
            0,
            0,
            0,
            (value, value, 0),
            (value, value, 0),
            (0, 0, 0),
            (0, 0, 0),
        )
    )

input_text = "".join(" ".join(map(str, (*c[:12], *c[12]))) + "\n" for c in cases)
native = subprocess.check_output(
    [sys.argv[1], "--trace-full"], input=input_text, text=True
).splitlines()
lookup_counts = [0, 0, 0]
for c, line in zip(cases, native):
    (
        cliffs,
        walls,
        hs,
        hr,
        hsrc,
        same,
        wall,
        ls,
        ld,
        rule,
        owner,
        allied,
        xyz,
        cell,
        srcxyz,
        dstxyz,
    ) = c
    target = sample if same else dest
    coords = {source: srcxyz, target: dstxyz, sample: cell, ref: (0, 0, 0)}
    if target == sample:
        coords[sample] = cell
    sample_level = ld if same else hs
    if not same and hs % 2:
        sample_level -= 4
    u.mem_write(sample + 0x11B, bytes([sample_level & 255]))
    w(sample + 0x140, 0x80 if hs != sample_level else 0)
    u.mem_write(ref + 0x11B, bytes([hr & 255]))
    w(ref + 0x140, 0)
    w(source + 0x140, 0x80 if hsrc != ls else 0)
    u.mem_write(projectile + 0x266, bytes([cliffs, 0, walls]))
    u.mem_write(source + 0x11B, bytes([ls & 255]))
    u.mem_write(target + 0x11B, bytes([ld & 255]))
    w(sample + 0x44, 0)
    w(sample + 0x50, 0)
    u.mem_write(overlay + 0x278, bytes([wall]))
    u.mem_write(rules + 0x1498, bytes([rule]))
    w(owners, base + 0x15000 if owner else 0)
    w(stack, stop)
    w(stack + 4, ref)
    for j, v in enumerate(xyz):
        w(stack + 8 + j * 4, v)
    w(stack + 20, projectile)
    w(stack + 24, base + 0x17000)
    u.reg_write(UC_X86_REG_ECX, source)
    u.reg_write(UC_X86_REG_EDX, target)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    lookup_coords.clear()
    u.emu_start(0x4BBAF0, stop, count=100000)
    hit, x, y = map(int, line.split())
    actual = u.reg_read(UC_X86_REG_EAX)
    assert actual == (sample if hit else 0), (c, actual, line, lookup_coords)
    assert lookup_coords[0] == (x, y), (c, lookup_coords, x, y)
    assert all(p == lookup_coords[0] for p in lookup_coords), (c, lookup_coords)
    lookup_counts[len(lookup_coords)] += 1
assert len(native) == len(cases)
print(
    f"Base game.exe: {len(cases)} complete terrain traces match; lookup counts {lookup_counts}"
)
