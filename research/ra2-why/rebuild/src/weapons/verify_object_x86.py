#!/usr/bin/env python3
"""Run original cell selection and projectile object collision in base game.exe."""

import random
import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import *

u = Uc(UC_ARCH_X86, UC_MODE_32)
u.mem_map(0x400000, 0x800000)
u.mem_write(0x400000, open("/path/to/user/ra2/game/game.exe", "rb").read()[:0x431000])
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
    global selected, collided
    if address == 0x548000:
        ret(cpu, cell, 1)
    elif address == stop + 0x100:
        sp = cpu.reg_read(UC_X86_REG_ESP)
        ptr = struct.unpack("<I", cpu.mem_read(sp + 4, 4))[0]
        cpu.mem_write(
            ptr, struct.pack("<iii", *virtual_positions[cpu.reg_read(UC_X86_REG_ECX)])
        )
        ret(cpu, ptr, 1)
    elif address == 0x4E5580:
        sp = cpu.reg_read(UC_X86_REG_ESP)
        obj = struct.unpack("<I", cpu.mem_read(sp + 4, 4))[0]
        ret(cpu, allies[obj], 1)
    elif address == 0x4608D8:
        selected = cpu.reg_read(UC_X86_REG_EAX)
    elif address in (0x4609E4, 0x460B33):
        collided = address == 0x460B33
        cpu.emu_stop()


for addr in (0x548000, stop + 0x100, 0x4E5580, 0x4608D8, 0x4609E4, 0x460B33):
    u.hook_add(UC_HOOK_CODE, hook, begin=addr, end=addr)
w(projectile + 0x98, kind)
w(vtable + 0x48, stop + 0x100)
rng = random.Random(0x4608B1)
cases = []
for _ in range(2048):
    pos = tuple(rng.randrange(-512, 513) for _ in range(3))
    count = rng.randrange(0, 5)
    objects = []
    for j in range(count):
        obj = object_base + j * 0x1000
        virt = tuple(rng.randrange(-300, 301) for _ in range(3))
        stored = tuple(
            v + rng.choice((-129, -128, -127, -10, 0, 10, 127, 128, 129)) for v in pos
        )
        objects.append((obj, virt, stored, rng.randrange(2), rng.randrange(2)))
    src = rng.choice([0, owner] + [o[0] for o in objects])
    preserve = rng.randrange(2)
    cases.append((pos, src, preserve, objects))


def line(c):
    pos, src, preserve, objects = c
    fields = [*pos, src, preserve, len(objects)]
    for obj, virt, stored, eligible, allied in objects:
        fields.extend([obj, *virt, *stored, eligible, allied])
    return " ".join(map(str, fields)) + "\n"


native = subprocess.check_output(
    [sys.argv[1], "--object-collision"], input="".join(map(line, cases)), text=True
).splitlines()
for c, expected in zip(cases, native):
    pos, src, preserve, objects = c
    virtual_positions = {o[0]: o[1] for o in objects}
    allies = {o[0]: o[4] for o in objects}
    selected = 0
    collided = None
    w(cell + 0xE4, objects[0][0] if objects else 0)
    for i, (obj, virt, stored, eligible, allied) in enumerate(objects):
        w(obj, vtable)
        w(obj + 0x30, objects[i + 1][0] if i + 1 < len(objects) else 0)
        u.mem_write(obj + 0x14, bytes([eligible]))
        for j, value in enumerate(stored):
            w(obj + 0x88 + j * 4, value)
        w(obj + 0x1B4, owner)
    if src:
        w(src + 0x1B4, owner)
    w(projectile + 0x9C, src)
    u.mem_write(kind + 0x272, bytes([preserve]))
    for j, value in enumerate(pos):
        w(stack + 0x24 + j * 4, value)
    u.reg_write(UC_X86_REG_EBP, projectile)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    u.emu_start(0x4608B1, stop, count=100000)
    actualpos = struct.unpack("<iii", u.mem_read(stack + 0x24, 12))
    assert collided is not None
    actual = (selected, int(collided), *actualpos)
    assert actual == tuple(map(int, expected.split())), (c, actual, expected)
assert len(native) == len(cases)
print(f"Base game.exe: {len(cases)} cell object selection and collision cases match")
