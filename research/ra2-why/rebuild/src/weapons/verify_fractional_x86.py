#!/usr/bin/env python3
"""Compare consecutive fractional Cannon ticks with the base executable.

Build from the repository root:
  g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
    rebuild/src/weapons/{weapons,fractional_tick_test}.cpp \
    rebuild/src/sim/{combat,target,distance}.cpp -o /tmp/ra2_fractional_tick
Run with Unicorn installed:
  RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_fractional_x86.py \
    /tmp/ra2_fractional_tick
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
        ret(cpu, 0, 1)
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

# Load an original 120mm ballistic launch for each horizontal direction.
# Its speed comes from the base game's gravity-6 ballistic rule.
cases = [
    (x, y, z)
    for x, y in (
        (512, 0),
        (512, 128),
        (512, 512),
        (128, 512),
        (0, 512),
        (-128, 512),
        (-512, 512),
        (-512, 128),
        (-512, 0),
        (-512, -128),
        (-512, -512),
        (-128, -512),
        (0, -512),
        (128, -512),
        (512, -512),
        (512, -128),
    )
    for z in (0, 32)
]
lines = subprocess.check_output(
    [sys.argv[1]], input="".join(f"{x} {y} {z}\n" for x, y, z in cases), text=True
).splitlines()
assert len(lines) == len(cases) * 9
rules, frame = base + 0x90000, base + 0x92000
w(0x839848, rules)
w(rules + 0x1320, 6)
w(kind + 0x2AC, 0)
u.mem_write(kind + 0x26B, b"\1")
u.mem_write(kind + 0x26C, b"\0")
u.mem_write(kind + 0x265, b"\0")
w(frame + 12, 0)
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
height_before = height_after = 500
virtual_positions = {}
allies = {}
in_impact = False
fractional_launches = 0
for i, (dx, dy, dz) in enumerate(cases):
    launch = lines[i * 9].split()
    speed, limited, heading, pitch = map(int, launch[:4])
    vector = tuple(map(float, launch[4:]))
    fractional_launches += any(v != int(v) for v in vector)
    w(stack + 0x7C, dx)
    w(stack + 0x80, dy)
    w(stack + 0x84, dz)
    w(stack + 0x28, speed)
    w(stack + 0x1C, kind)
    u.reg_write(UC_X86_REG_EAX, dy & 0xFFFFFFFF)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    u.emu_start(0x6CB062, 0x6CB15E, count=100000)
    orig_limited = struct.unpack("<i", u.mem_read(stack + 0x28, 4))[0]
    orig_heading = struct.unpack("<H", u.mem_read(stack + 0x20, 2))[0]
    w(stack + 0x1C, 0)
    w(stack + 0x2C, projectile)
    u.reg_write(UC_X86_REG_EDI, dz & 0xFFFFFFFF)
    u.reg_write(UC_X86_REG_EBP, frame)
    u.emu_start(0x6CB1AC, 0x6CB747, count=100000)
    orig_pitch = struct.unpack("<H", u.mem_read(stack + 0x60, 2))[0]
    orig_vector = struct.unpack("<ddd", u.mem_read(stack + 0x3C, 24))
    assert (orig_limited, orig_heading, orig_pitch, *orig_vector) == (
        limited,
        heading,
        pitch,
        *vector,
    ), (cases[i], launch, orig_vector)
    pos = (0, 0, 2000)
    for step in range(8):
        for j, value in enumerate(pos):
            w(projectile + 0x88 + j * 4, value)
            w(stack + 0xA8 + j * 4, value)
            u.mem_write(stack + 0x78 + j * 8, struct.pack("<d", value))
        for j, value in enumerate(orig_vector):
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
        pos = struct.unpack("<iii", u.mem_read(stack + 0x24, 12))
        orig_vector = struct.unpack("<ddd", u.mem_read(projectile + 0xD0, 24))
        assert mode == 0
        # Run the original impact function with the separately tested trace result.
        for j, value in enumerate(pos):
            w(projectile + 0x88 + j * 4, value)
        w(stack - 0x300, stop)
        w(stack - 0x2FC, base + 0x82000)
        u.reg_write(UC_X86_REG_ESP, stack - 0x300)
        u.reg_write(UC_X86_REG_ECX, projectile)
        in_impact = True
        u.emu_start(0x461C10, stop, count=10000)
        assert (u.reg_read(UC_X86_REG_EAX) & 255) == 0
        fields = lines[i * 9 + step + 1].split()
        expected = (*map(int, fields[:4]), *map(float, fields[4:]))
        assert (mode, *pos, *orig_vector) == expected, (
            cases[i],
            step,
            (mode, *pos, *orig_vector),
            expected,
        )
assert fractional_launches == len(cases)
print(
    f"Base game.exe: {len(cases)} fractional original launches and {len(cases)*8} consecutive tick snapshots match"
)
