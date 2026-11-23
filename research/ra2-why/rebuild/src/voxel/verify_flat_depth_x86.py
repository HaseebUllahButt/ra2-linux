#!/usr/bin/env python3
"""Run base-game 0x4C9930 with an all-flat synthetic map."""

import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_READ
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_ECX,
    UC_X86_REG_EIP,
    UC_X86_REG_ESP,
    UC_X86_REG_FPCW,
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
# Distinct neighboring cells exercise the original map checks. The two
# occupied cells use the original cell object-list head at +0xE4.
cells = {}
for y in range(7, 14):
    for x in range(7, 14):
        ptr = base + 0x10000 + ((y - 7) * 7 + x - 7) * 0x200
        cells[x, y] = ptr
        w(ptr + 0x24, x | (y << 16))
        w(ptr + 0x38, 0)
        w(ptr + 0x44, 0xFFFFFFFF)
        u.mem_write(ptr + 0x116, b"\xff\xff")
w(coords, 10 | (10 << 16))
w(cells[10, 10] + 0xE4, obj)
w(cells[11, 10] + 0xE4, obj)
directions = ((0, -1), (1, -1), (1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1))
for index, (dx, dy) in enumerate(directions):
    u.mem_write(0x851A70 + index * 4, struct.pack("<hh", dx, dy))
w(0xA40CD4, base + 0x8000)
w(0xA52F00, 0)
w(base + 0x8000, base + 0x9000)
w(base + 0x9000, base + 0xA000)
w(base + 0xA000 + 0x9C, 0x3000040)
w(base + 0xB000, 1)
w(base + 0xB004, 1)
w(base + 0xB008, 60)
w(base + 0xB00C, 30)
# Execute the game's default scale initialization with normal x87 precision.
u.reg_write(UC_X86_REG_FPCW, 0x27F)
for address in (0x6A20E0, 0x6A2170, 0x6A2440):
    w(stack, stop)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.emu_start(address, stop, count=1000000)
scale = struct.unpack("<d", u.mem_read(0xABE490, 8))[0]
assert scale == 0.14350360082660085


def hook(cpu, address, size, unused):
    if address == 0x3000010:
        ret(cpu, kind)
    elif address == 0x3000020:
        ret(cpu, coords, 1)
    elif address == 0x3000030:
        ret(cpu, 1)
    elif address == 0x548070:
        sp = cpu.reg_read(UC_X86_REG_ESP)
        packed = read(read(sp + 4))
        x, y = struct.unpack("<hh", struct.pack("<I", packed))
        ret(cpu, cells.get((x, y), 0), 1)
    elif address == 0x4B8BE0:
        ret(cpu, face, 1)
    elif address == 0x3000040:
        ret(cpu, base + 0xB000)


u.hook_add(UC_HOOK_CODE, hook)
occupancy_reads = []


def occupancy_hook(cpu, access, address, size, value, unused):
    if (address - (base + 0x10000)) % 0x200 == 0xE4:
        occupancy_reads.append(address)


u.hook_add(UC_HOOK_MEM_READ, occupancy_hook, begin=base + 0x10000, end=base + 0x1FFFF)
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
        expected = -(int(altitude * scale + (altitude >= 728) + 0.5)) - 1
        assert actual == expected, (altitude, facing, actual, expected)
        projected_heights.append(-expected - 1)
        expected_results.append(actual)
# Equal raised ground remains flat. This checks the signed cell level in
# every sampled neighbor, together with a matching vehicle world height.
for level in range(1, 16):
    for ptr in cells.values():
        u.mem_write(ptr + 0x11B, bytes([level]))
    altitude = level * 256
    for facing in range(0, 65536, 2048):
        w(obj + 0x90, altitude)
        w(face, facing)
        w(stack, stop)
        u.reg_write(UC_X86_REG_ESP, stack)
        u.reg_write(UC_X86_REG_ECX, obj)
        u.emu_start(0x4C9930, stop, count=1000000)
        actual = struct.unpack("<i", struct.pack("<I", u.reg_read(UC_X86_REG_EAX)))[0]
        expected = -int(altitude * scale + (altitude >= 728) + 0.5) - 1
        assert actual == expected, (level, facing, actual, expected)
        projected_heights.append(-expected - 1)
        expected_results.append(actual)
for ptr in cells.values():
    u.mem_write(ptr + 0x11B, b"\0")
if len(sys.argv) != 2:
    raise SystemExit("usage: verify_flat_depth_x86.py voxel_test")
native = subprocess.check_output(
    [sys.argv[1], "--flat-depth-adjust"],
    input="".join(f"{height}\n" for height in projected_heights),
    text=True,
)
assert list(map(int, native.split())) == expected_results
assert not occupancy_reads
# Perturb one cell at a time. Bits 0x100 and 0x400 enter the bridge-related
# checks. Bit 0x10000 changes a tile-shape check and needs separate evidence.
clear_bits = [1 << bit for bit in range(32) if (1 << bit) & 0x10500 == 0]
clear_bits.append(0xFFFFFFFF ^ 0x10500)
flag_checks = 0
for bit in clear_bits:
    for y in range(9, 12):
        for x in range(9, 12):
            w(cells[x, y] + 0x140, bit)
            for facing in (0, 0x4000, 0x8000, 0xC000):
                w(obj + 0x90, 0)
                w(face, facing)
                w(stack, stop)
                u.reg_write(UC_X86_REG_ESP, stack)
                u.reg_write(UC_X86_REG_ECX, obj)
                u.emu_start(0x4C9930, stop, count=1000000)
                actual = struct.unpack(
                    "<i", struct.pack("<I", u.reg_read(UC_X86_REG_EAX))
                )[0]
                assert actual == -1, (bit, x, y, facing, actual)
                flag_checks += 1
            w(cells[x, y] + 0x140, 0)
assert not occupancy_reads
special = {}
for bit in (0x100, 0x400, 0x10000):
    w(cells[10, 10] + 0x140, bit)
    w(face, 0)
    w(stack, stop)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_ECX, obj)
    u.emu_start(0x4C9930, stop, count=1000000)
    special[bit] = struct.unpack("<i", struct.pack("<I", u.reg_read(UC_X86_REG_EAX)))[0]
    w(cells[10, 10] + 0x140, 0)
assert special[0x100] == 58, special
print(f"Base game.exe: 640 flat views and {flag_checks} cell flag cases match")
