#!/usr/bin/env python3
"""Compare guarded projectile movement and collision with base game.exe."""

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
rng = random.Random(0x460397)
cases = []
for _ in range(1536):
    pos = (rng.randrange(-512, 513), rng.randrange(-512, 513), rng.randrange(500, 1001))
    vel = (rng.randrange(-80, 81), rng.randrange(-80, 81), rng.randrange(-60, 61))
    if sum(v * v for v in vel) < 100:
        vel = (20, 0, 0)
    gravity = rng.choice((0, 3, 6, 12))
    proposal = (pos[0] + vel[0], pos[1] + vel[1], pos[2] + vel[2] - gravity)
    target = (
        proposal[0] + rng.choice((0, 0, 512, 1024)),
        proposal[1] + rng.choice((0, 512, 1024)),
        0,
    )
    height_before = rng.choice((5, 10, 100, 208, 300))
    count = rng.randrange(0, 4)
    objects = []
    for j in range(count):
        obj = object_base + j * 0x1000
        virt = tuple(rng.randrange(-256, 257) for _ in range(3))
        stored = tuple(v + rng.choice((-128, -40, 0, 40, 128)) for v in proposal)
        objects.append((obj, virt, stored, rng.randrange(2), rng.randrange(2)))
    src = rng.choice([0, owner] + [o[0] for o in objects])
    preserve, building_flag, map_valid, trace_hit = (rng.randrange(2) for _ in range(4))
    height_after = rng.choice((500, -417, -416, -415))
    cases.append(
        (
            pos,
            vel,
            gravity,
            target,
            height_before,
            src,
            preserve,
            building_flag,
            map_valid,
            trace_hit,
            height_after,
            objects,
        )
    )


def line(c):
    (
        pos,
        vel,
        gravity,
        target,
        height,
        src,
        preserve,
        build,
        valid,
        trace_hit,
        height_after,
        objects,
    ) = c
    fields = [
        *pos,
        *vel,
        gravity,
        *target,
        height,
        height_after,
        104,
        src,
        preserve,
        build,
        build,
        valid,
        trace_hit,
        len(objects),
    ]
    for obj, virt, stored, eligible, allied in objects:
        fields.extend([obj, *virt, *stored, eligible, allied])
    return " ".join(map(str, fields)) + "\n"


native = subprocess.check_output(
    [sys.argv[1], "--flat-tick"], input="".join(map(line, cases)), text=True
).splitlines()
for case, expected in zip(cases, native):
    (
        pos,
        vel,
        gravity,
        target,
        height_before,
        src,
        preserve,
        build,
        map_valid,
        trace_hit,
        height_after,
        objects,
    ) = case
    virtual_positions = {o[0]: o[1] for o in objects}
    allies = {o[0]: o[4] for o in objects}
    in_impact = False
    selected = hit_object = 0
    collided = None
    building = base + 0x50000 if build else 0
    w(cell + 0xE4, objects[0][0] if objects else 0)
    w(cell + 0x140, 0)
    for i, (obj, virt, stored, eligible, allied) in enumerate(objects):
        w(obj, vtable)
        w(obj + 0x30, objects[i + 1][0] if i + 1 < len(objects) else 0)
        u.mem_write(obj + 0x14, bytes([eligible]))
        w(obj + 0x1B4, owner)
        for j, value in enumerate(stored):
            w(obj + 0x88 + j * 4, value)
    if src:
        w(src + 0x1B4, owner)
    w(projectile + 0x9C, src)
    u.mem_write(kind + 0x272, bytes([preserve]))
    u.mem_write(kind + 0x290, b"\0")
    for j, value in enumerate(pos):
        w(projectile + 0x88 + j * 4, value)
        w(stack + 0xA8 + j * 4, value)
        u.mem_write(stack + 0x78 + j * 8, struct.pack("<d", value))
    for j, value in enumerate(vel):
        u.mem_write(stack + 0x90 + j * 8, struct.pack("<d", value))
        u.mem_write(projectile + 0xD0 + j * 8, struct.pack("<d", value))
    w(projectile + 0x128, target[0])
    w(projectile + 0x12C, target[1])
    u.mem_write(gravity_ptr, struct.pack("<d", gravity))
    w(stack + 0x20, 0)
    u.mem_write(stack + 0x18, b"\0")
    u.mem_write(stack + 0x1F, b"\0")
    u.reg_write(UC_X86_REG_EAX, kind)
    u.reg_write(UC_X86_REG_EBP, projectile)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    try:
        u.emu_start(stub, stop, count=100000)
    except Exception:
        print(
            "EIP",
            hex(u.reg_read(UC_X86_REG_EIP)),
            "ECX",
            hex(u.reg_read(UC_X86_REG_ECX)),
            "case",
            case,
        )
        raise
    assert collided
    mode = struct.unpack("<I", u.mem_read(stack + 0x20, 4))[0]
    position = struct.unpack("<iii", u.mem_read(stack + 0x24, 12))
    if mode == 0:
        for j, value in enumerate(position):
            w(projectile + 0x88 + j * 4, value)
        u.mem_write(kind + 0x266, b"\1")
        u.mem_write(kind + 0x268, b"\1")
        w(stack - 0x300, stop)
        w(stack - 0x2FC, base + 0x82000)
        u.reg_write(UC_X86_REG_ESP, stack - 0x300)
        u.reg_write(UC_X86_REG_ECX, projectile)
        in_impact = True
        try:
            u.emu_start(0x461C10, stop, count=10000)
        except Exception:
            print("impact EIP", hex(u.reg_read(UC_X86_REG_EIP)), "case", case)
            raise
        if u.reg_read(UC_X86_REG_EAX) & 255:
            mode = 1
    velocity = struct.unpack("<ddd", u.mem_read(projectile + 0xD0, 24))
    vals = expected.split()
    actual = (mode, hit_object, *position, *velocity)
    wanted = (*map(int, vals[:5]), *map(float, vals[5:]))
    assert actual == wanted, (case, actual, wanted)
assert len(native) == len(cases)
print(
    f"Base game.exe: {len(cases)} connected flat projectile motion and object collision cases match"
)
