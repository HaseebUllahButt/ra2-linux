#!/usr/bin/env python3
"""Compare original base projectile arithmetic using Unicorn and real game.exe."""

import random
import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_ECX,
    UC_X86_REG_ESP,
    UC_X86_REG_FPCW,
    UC_X86_REG_EBX,
    UC_X86_REG_EDX,
    UC_X86_REG_EDI,
    UC_X86_REG_ESI,
    UC_X86_REG_EIP,
)

if len(sys.argv) != 2:
    raise SystemExit("usage: verify_base_x86.py weapons_test")
exe = open("/path/to/user/ra2/game/game.exe", "rb").read()
u = Uc(UC_ARCH_X86, UC_MODE_32)
u.mem_map(0x400000, 0x800000)
u.mem_write(0x400000, exe[:0x431000])
u.mem_map(0x1000000, 0x100000)
u.mem_map(0x2000000, 0x100000)
u.mem_map(0x3000000, 0x1000)
base, stack, stop = 0x1000000, 0x20FFF00, 0x3000000


def w(p, v):
    u.mem_write(p, struct.pack("<I", v & 0xFFFFFFFF))


def f(p, v):
    u.mem_write(p, struct.pack("<d", v))


def s(v):
    return struct.unpack("<i", struct.pack("<I", v))[0]


speeds = [
    (distance, gravity)
    for distance in (0, 1, 64, 128, 256, 512, 1024, 1472, 4096, 10000)
    for gravity in (1, 3, 6, 12)
]
input_text = "".join(f"{d} {g}\n" for d, g in speeds)
native = list(
    map(
        int,
        subprocess.check_output(
            [sys.argv[1], "--speed"], input=input_text, text=True
        ).splitlines(),
    )
)
for (distance, gravity), expected in zip(speeds, native):
    w(stack, stop)
    f(stack + 4, gravity)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_ECX, distance)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    u.emu_start(0x47FB70, stop, count=100000)
    actual = s(u.reg_read(UC_X86_REG_EAX))
    assert actual == expected, ((distance, gravity), actual, expected)
assert len(native) == len(speeds)

# Original ballistic angle and lookup table. Cases stay in the verified
# square-root domain used by the native shared distance primitive.
angle_cases = [
    (v, d, h, gravity, a)
    for v in (80, 120, 200)
    for d in (100, 256, 512, 1000)
    for h in (-32, 0, 32)
    for gravity in (3, 6, 12)
    for a in (0, 1)
    if (v, d, h, gravity, a) != (200, 100, 0, 3, 1)
]
angle_cases += [(40, 1000, 0, 6, a) for a in (0, 1)]
angle_cases += [(80, 256, 0, 0, a) for a in (0, 1)]
angle_cases += [
    (native[i], distance, 0, gravity, alternate)
    for i, (distance, gravity) in enumerate(speeds)
    if gravity == 6 and distance >= 64
    for alternate in (0, 1)
]
angle_input = "".join(" ".join(map(str, c)) + "\n" for c in angle_cases)
angle_native = subprocess.check_output(
    [sys.argv[1], "--angle"], input=angle_input, text=True
).splitlines()
for case, expected in zip(angle_cases, angle_native):
    speed, distance, height, gravity, alternate = case
    w(stack, stop)
    w(stack + 4, distance)
    w(stack + 8, height)
    f(stack + 12, gravity)
    w(stack + 20, base + 0x200)
    u.reg_write(UC_X86_REG_ECX, alternate)
    u.reg_write(UC_X86_REG_EDX, speed)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    u.emu_start(0x47F8B0, stop, count=100000)
    result = (
        struct.unpack("<H", u.mem_read(base + 0x200, 2))[0]
        if u.reg_read(UC_X86_REG_EAX) & 255
        else -1
    )
    assert result == int(expected), (case, result, expected)
assert len(angle_native) == len(angle_cases)

# Load gravity onto x87, then execute the actual arc update instructions.
# At 460397 the original x87 stack already holds gravity.
stub = base + 0x5000
gravity_ptr = base + 0x5100
code = b"\xdd\x05" + struct.pack("<I", gravity_ptr)
code += b"\xe9" + struct.pack("<i", 0x460397 - (stub + len(code) + 5))
u.mem_write(stub, code)
rng = random.Random(0x460397)
cases = []
for _ in range(512):
    position = tuple(rng.randrange(-3000, 3001) for _ in range(3))
    velocity = tuple(rng.randrange(-2048, 2049) / 8 for _ in range(3))
    gravity = rng.choice((0, 1, 3, 6, 12))
    cases.append((*position, *velocity, gravity))
for _ in range(512):
    position = tuple(rng.randrange(-3000, 3001) for _ in range(3))
    velocity = tuple(rng.randrange(-10000, 10001) / 10 for _ in range(3))
    gravity = rng.choice((0, 0.1, 1, 3, 6, 12))
    cases.append((*position, *velocity, gravity))
input_text = "".join(" ".join(map(str, c)) + "\n" for c in cases)
native = subprocess.check_output(
    [sys.argv[1], "--arc"], input=input_text, text=True
).splitlines()
for case, line in zip(cases, native):
    x, y, z, vx, vy, vz, gravity = case
    for i, value in enumerate((x, y, z)):
        f(stack + 0x78 + i * 8, value)
    for i, value in enumerate((vx, vy, vz)):
        f(stack + 0x90 + i * 8, value)
    f(gravity_ptr, gravity)
    w(base + 0x1000 + 0x9C, 0)
    f(base + 0x2000 + 0x298, 0)
    u.reg_write(UC_X86_REG_EAX, base + 0x2000)
    u.reg_write(UC_X86_REG_EBP, base + 0x1000)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    u.emu_start(stub, 0x460435, count=100000)
    pos = struct.unpack("<3i", u.mem_read(stack + 0x44, 12))
    vels = struct.unpack("<3d", u.mem_read(stack + 0x90, 24))
    values = line.split()
    expected_pos = tuple(map(int, values[:3]))
    expected_vel = tuple(map(float, values[3:]))
    assert pos == expected_pos and vels == expected_vel, (
        case,
        pos,
        expected_pos,
        vels,
        expected_vel,
    )
assert len(native) == len(cases)
# Enter the real standard-fire fragment. Intercept projectile allocation.
weapon, shooter, target, projectile, warhead, rule = (
    base + 0x50000,
    base + 0x1000,
    base + 0x2000,
    base + 0x3000,
    base + 0x4000,
    base + 0x60000,
)
w(0x839848, rule)
w(weapon + 0xA0, projectile)
w(weapon + 0xAC, warhead)
w(projectile + 0x2AC, 0)
u.mem_write(projectile + 0x265, b"\0")
captured = []
distances = []


def hook(cpu, address, size, unused):
    if address == 0x733F70:
        distances.append(
            struct.unpack("<i", cpu.mem_read(cpu.reg_read(UC_X86_REG_ESP) + 4, 4))[0]
        )
    elif address == 0x463990:
        sp = cpu.reg_read(UC_X86_REG_ESP)
        args = struct.unpack("<5I", cpu.mem_read(sp + 4, 20))
        captured.append(
            (
                args[0],
                cpu.reg_read(UC_X86_REG_EDX),
                cpu.reg_read(UC_X86_REG_ECX),
                args[2],
                s(args[1]),
                distances[-1],
                s(args[3]),
                args[4] & 255,
            )
        )
        cpu.emu_stop()


u.hook_add(UC_HOOK_CODE, hook)
fire_cases = []
for _ in range(256):
    muzzle = (rng.randrange(0, 10000), rng.randrange(0, 10000))
    target_xy = (rng.randrange(0, 10000), rng.randrange(0, 10000))
    fire_cases.append(
        (
            *muzzle,
            *target_xy,
            rng.choice((35, 90, 120)),
            rng.choice((3, 6, 12)),
            rng.randrange(2),
        )
    )
fire_input = "".join(" ".join(map(str, c)) + "\n" for c in fire_cases)
fire_native = subprocess.check_output(
    [sys.argv[1], "--fire"], input=fire_input, text=True
).splitlines()
for case, line in zip(fire_cases, fire_native):
    mx, my, tx, ty, damage, gravity, flag = case
    w(stack + 0x54, mx)
    w(stack + 0x58, my)
    w(stack + 0xA4, tx)
    w(stack + 0xA8, ty)
    w(base + 0x8000 + 8, target)
    w(rule + 0x1320, gravity)
    u.mem_write(weapon + 0x10B, bytes([flag]))
    u.reg_write(UC_X86_REG_EBX, weapon)
    u.reg_write(UC_X86_REG_ESI, shooter)
    u.reg_write(UC_X86_REG_EDI, damage)
    u.reg_write(UC_X86_REG_EBP, base + 0x8000)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    u.emu_start(0x6CAC65, stop, count=100000)
    expected = tuple(map(int, line.split()))
    assert captured[-1] == expected, (case, captured[-1], expected)
assert len(captured) == len(fire_cases) == len(fire_native)
# Execute Cannon's original impact check. Resolve its map trace and height
# virtual through deterministic stubs, as the simulation must do.
bullet, vtable, bullet_type, cell = (
    base + 0x90000,
    base + 0x91000,
    base + 0x92000,
    base + 0x93000,
)
w(bullet, vtable)
w(bullet + 0x98, bullet_type)
w(vtable + 0x1B0, stop + 0x100)
u.mem_write(bullet_type + 0x266, b"\1")
u.mem_write(bullet_type + 0x268, b"\1")
trace_result = 0
height_result = 0


def return_stub(cpu, value, args):
    sp = cpu.reg_read(UC_X86_REG_ESP)
    retaddr = struct.unpack("<I", cpu.mem_read(sp, 4))[0]
    cpu.reg_write(UC_X86_REG_EAX, value & 0xFFFFFFFF)
    cpu.reg_write(UC_X86_REG_ESP, sp + 4 + args * 4)
    cpu.reg_write(UC_X86_REG_EIP, retaddr)


def impact_hook(cpu, address, size, unused):
    if address in (0x548000, 0x548070):
        return_stub(cpu, cell, 1)
    elif address == 0x4BBAF0:
        return_stub(cpu, trace_result, 6)
    elif address == stop + 0x100:
        return_stub(cpu, height_result, 0)


u.hook_add(UC_HOOK_CODE, impact_hook)
impact_cases = [
    (hit, height, unit)
    for unit in (1, 10, 60, 256)
    for height in (-1025, -1024, -241, -240, -41, -40, -5, -4, -1, 0, 1, 100)
    for hit in (0, 1)
]
impact_input = "".join(" ".join(map(str, c)) + "\n" for c in impact_cases)
impact_native = subprocess.check_output(
    [sys.argv[1], "--impact"], input=impact_input, text=True
).splitlines()
for case, line in zip(impact_cases, impact_native):
    trace_result, height_result, unit = case
    w(0x850348, unit)
    w(stack, stop)
    w(stack + 4, base + 0x94000)
    u.reg_write(UC_X86_REG_ECX, bullet)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.emu_start(0x461C10, stop, count=100000)
    actual = u.reg_read(UC_X86_REG_EAX) & 255
    assert actual == int(line), (case, actual, line)
assert len(impact_native) == len(impact_cases)
print(
    f"Base game.exe: {len(speeds)} ballistic speeds, {len(angle_cases)} launch angles, {len(cases)} arc proposals, {len(fire_cases)} fire events, and {len(impact_cases)} impact checks match"
)
