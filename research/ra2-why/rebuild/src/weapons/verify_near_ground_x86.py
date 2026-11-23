#!/usr/bin/env python3
"""Compare near-ground ticks using cells from the original base map.

Build in the parent checkout:
  g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src -I rebuild/src/render \
    rebuild/src/weapons/{weapons,near_ground_test}.cpp \
    rebuild/src/sim/{combat,target,distance,floor,world,world_init,replacement,test_inputs}.cpp \
    rebuild/src/render/{minimap,tmp_decode}.cpp rebuild/src/gfx/palette.cpp \
    rebuild/src/fs/{mix_archive,mix_bootstrap,filename_hash}.cpp \
    rebuild/src/rules/ini_file.cpp rebuild/src/map/map_file.cpp \
    rebuild/src/pack/map_pack.cpp -ldl -o /tmp/ra2_near_ground
Run with Unicorn installed:
  RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_near_ground_x86.py \
    /tmp/ra2_near_ground
"""

import os
from pathlib import Path
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
cell, projectile, kind, vtable, owner = (
    base + n for n in (0x1000, 0x3000, 0x5000, 0x7000, 0x9000)
)
object_base = base + 0x10000


def w(p, v):
    u.mem_write(p, struct.pack("<I", v & 0xFFFFFFFF))


def ret(cpu, value, count):
    sp = cpu.reg_read(UC_X86_REG_ESP)
    cpu.reg_write(UC_X86_REG_EAX, value & 0xFFFFFFFF)
    cpu.reg_write(UC_X86_REG_EIP, struct.unpack("<I", cpu.mem_read(sp, 4))[0])
    cpu.reg_write(UC_X86_REG_ESP, sp + 4 + count * 4)


virtual_positions, allies = {}, {}
selected, collided = 0, None


def hook(cpu, address, size, user):
    global selected, collided, hit_object
    if address == 0x548000:
        ret(cpu, cell, 1)
    elif address == stop + 0x100:
        sp = cpu.reg_read(UC_X86_REG_ESP)
        ptr = struct.unpack("<I", cpu.mem_read(sp + 4, 4))[0]
        cpu.mem_write(
            ptr, struct.pack("<iii", *virtual_positions[cpu.reg_read(UC_X86_REG_ECX)])
        )
        ret(cpu, ptr, 1)
    elif address == 0x6D7D20:
        ret(cpu, 0, 1)
    elif address == 0x55A280:
        ret(cpu, current_floor, 1)
    elif address == 0x475840:
        ret(cpu, 0, 2)
    elif address == 0x548070:
        ret(cpu, cell, 1)
    elif address == 0x4717B0:
        ret(cpu, building, 0)
    elif address == stop + 0x110:
        ret(cpu, height_after if in_impact else height_before, 0)
    elif address == 0x4BBAF0:
        ret(cpu, cell if trace_hit else 0, 6)
    elif address == 0x54AB00:
        ret(cpu, map_valid, 1)
    elif address == 0x4609A4:
        hit_object = cpu.reg_read(UC_X86_REG_ESI)
    elif address == 0x4E5580:
        sp = cpu.reg_read(UC_X86_REG_ESP)
        obj = struct.unpack("<I", cpu.mem_read(sp + 4, 4))[0]
        ret(cpu, allies[obj], 1)
    elif address == 0x4608D8:
        selected = cpu.reg_read(UC_X86_REG_EAX)
    elif address == 0x460B33:
        collided = True
        cpu.emu_stop()


for addr in (
    0x548000,
    stop + 0x100,
    0x4E5580,
    0x4608D8,
    0x460B33,
    0x55A280,
    0x548070,
    0x4717B0,
    stop + 0x110,
    0x54AB00,
    0x4609A4,
    0x4BBAF0,
    0x6D7D20,
    0x475840,
):
    u.hook_add(UC_HOOK_CODE, hook, begin=addr, end=addr)
w(projectile + 0x98, kind)
w(vtable + 0x48, stop + 0x100)
w(vtable + 0x1B0, stop + 0x110)
w(projectile, vtable)

stub, gravity_ptr = base + 0x80000, base + 0x81000
code = b"\xdd\x05" + struct.pack("<I", gravity_ptr)
code += b"\xe9" + struct.pack("<i", 0x460397 - (stub + len(code) + 5))
u.mem_write(stub, code)
w(0x850348, 104)
w(0x850338, 0)


# The fixture performs the original map and theater setup before selecting cells.
assets = subprocess.check_output([sys.argv[1], "--assets"], text=True).splitlines()
cells = [tuple(map(int, line.split())) for line in assets]
assert len(cells) == 32
cases = [
    (*c, offset, 1)
    for c in cells
    for offset in (-100, -30, 0, 1, 100, 149, 150, 151, 200)
]
input_text = "".join(" ".join(map(str, c)) + "\n" for c in cases)
native = subprocess.check_output(
    [sys.argv[1], "--tick"], input=input_text, text=True
).splitlines()
w(projectile + 0x9C, owner)
w(owner + 0x1B4, owner)
u.mem_write(kind + 0x290, b"\0")
u.mem_write(kind + 0x266, b"\1")
u.mem_write(kind + 0x268, b"\1")
w(cell + 0xE4, 0)
w(cell + 0x140, 0)
w(projectile + 0x128, 100000)
w(projectile + 0x12C, 100000)
building = 0
map_valid = 1
trace_hit = 0
virtual_positions = {}
allies = {}
for case, line in zip(cases, native):
    x, y, level, current_floor, offset, source = case
    pos = (x * 256 + 128, y * 256 + 128, current_floor + offset)
    vel = (20, 0, -12)
    height_before = offset
    height_after = offset - 18
    for j, value in enumerate(pos):
        w(projectile + 0x88 + j * 4, value)
        w(stack + 0xA8 + j * 4, value)
        u.mem_write(stack + 0x78 + j * 8, struct.pack("<d", value))
    for j, value in enumerate(vel):
        u.mem_write(stack + 0x90 + j * 8, struct.pack("<d", value))
        u.mem_write(projectile + 0xD0 + j * 8, struct.pack("<d", value))
    u.mem_write(gravity_ptr, struct.pack("<d", 6))
    w(stack + 0x20, 0)
    u.mem_write(stack + 0x18, b"\0")
    u.mem_write(stack + 0x1F, b"\0")
    u.reg_write(UC_X86_REG_EAX, kind)
    u.reg_write(UC_X86_REG_EBP, projectile)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    collided = None
    in_impact = False
    u.emu_start(stub, stop, count=100000)
    assert collided
    mode = struct.unpack("<I", u.mem_read(stack + 0x20, 4))[0]
    result_pos = struct.unpack("<iii", u.mem_read(stack + 0x24, 12))
    result_vel = struct.unpack("<ddd", u.mem_read(projectile + 0xD0, 24))
    if mode == 0:
        for j, value in enumerate(result_pos):
            w(projectile + 0x88 + j * 4, value)
        w(stack - 0x300, stop)
        w(stack - 0x2FC, base + 0x82000)
        u.reg_write(UC_X86_REG_ESP, stack - 0x300)
        u.reg_write(UC_X86_REG_ECX, projectile)
        in_impact = True
        u.emu_start(0x461C10, stop, count=10000)
        if u.reg_read(UC_X86_REG_EAX) & 255:
            mode = 1
    vals = line.split()
    expected = (*map(int, vals[:4]), *map(float, vals[4:]))
    assert (mode, *result_pos, *result_vel) == expected, (
        case,
        (mode, *result_pos, *result_vel),
        expected,
    )
# Without a source, the original bounce path is still unresolved.
ownerless = [(x, y, level, floor, 100, 0) for x, y, level, floor in cells]
ownerless_input = "".join(" ".join(map(str, c)) + "\n" for c in ownerless)
ownerless_native = subprocess.check_output(
    [sys.argv[1], "--tick"], input=ownerless_input, text=True
).splitlines()
assert len(ownerless_native) == len(ownerless)
assert all(int(line.split()[0]) == 3 for line in ownerless_native)
assert len(native) == len(cases)
print(
    f"Base game.exe: {len(cases)} near-ground ticks match on {len(cells)} initialized ALL02S.MAP cells"
)
