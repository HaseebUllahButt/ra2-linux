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

# Original target-to-heading conversion, including the four axes.
heading_rng = random.Random(0x6CB062)
heading_cases = [
    (heading_rng.randrange(-10000, 10001), heading_rng.randrange(-10000, 10001))
    for _ in range(512)
]
heading_cases += [(0, 0), (0, 1), (0, -1), (-1, 0), (1, 0)]
heading_input = "".join(" ".join(map(str, c)) + "\n" for c in heading_cases)
heading_native = subprocess.check_output(
    [sys.argv[1], "--heading-angle"], input=heading_input, text=True
).splitlines()
for case, line in zip(heading_cases, heading_native):
    x, y = case
    w(stack + 0x7C, x)
    u.reg_write(UC_X86_REG_EAX, y & 0xFFFFFFFF)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    u.emu_start(0x6CB062, 0x6CB0A2, count=100000)
    actual = struct.unpack("<H", u.mem_read(stack + 0x20, 2))[0]
    assert actual == int(line), (case, actual, line)
assert len(heading_native) == len(heading_cases)

# Normal non-burst horizontal velocity from the resolved heading and speed.
velocity_cases = [
    (heading_rng.randrange(2, 1000), heading_rng.randrange(65536)) for _ in range(512)
]
velocity_cases += [(0, 0), (0, 0x3FFF)]
velocity_input = "".join(" ".join(map(str, c)) + "\n" for c in velocity_cases)
velocity_native = subprocess.check_output(
    [sys.argv[1], "--heading"], input=velocity_input, text=True
).splitlines()
for case, line in zip(velocity_cases, velocity_native):
    speed, heading = case
    w(stack + 0x28, speed)
    w(stack + 0x1C, 0)
    u.mem_write(stack + 0x20, struct.pack("<H", heading))
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    u.emu_start(0x6CB1AC, 0x6CB3F1, count=100000)
    actual = struct.unpack("<3d", u.mem_read(stack + 0x3C, 24))
    expected = tuple(map(float, line.split()))
    assert actual == expected, (case, actual, expected)
assert len(velocity_native) == len(velocity_cases)

# The original caller limits initial speed to half the 3-D displacement.
limit_cases = [
    (
        heading_rng.randrange(0, 1000),
        heading_rng.randrange(-3000, 3001),
        heading_rng.randrange(-3000, 3001),
        heading_rng.randrange(-3000, 3001),
    )
    for _ in range(512)
]
limit_input = "".join(" ".join(map(str, c)) + "\n" for c in limit_cases)
limit_native = subprocess.check_output(
    [sys.argv[1], "--limit-speed"], input=limit_input, text=True
).splitlines()
for case, line in zip(limit_cases, limit_native):
    speed, x, y, z = case
    w(stack + 0x28, speed)
    w(stack + 0x7C, x)
    w(stack + 0x80, y)
    w(stack + 0x84, z)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    u.emu_start(0x6CB0E7, 0x6CB15E, count=100000)
    actual = struct.unpack("<i", u.mem_read(stack + 0x28, 4))[0]
    assert actual == int(line), (case, actual, line)
assert len(limit_native) == len(limit_cases)

# Reorient a resolved three-dimensional launch vector with the original
# trigonometric tables. Positions and firing eligibility remain caller inputs.
pitch_rng = random.Random(0x6CB605)
pitch_cases = [
    (
        pitch_rng.randrange(-1000, 1001) / 10,
        pitch_rng.randrange(-1000, 1001) / 10,
        pitch_rng.randrange(-300, 301) / 10,
        pitch_rng.randrange(65536),
    )
    for _ in range(512)
]
pitch_input = "".join(" ".join(map(str, c)) + "\n" for c in pitch_cases)
pitch_native = subprocess.check_output(
    [sys.argv[1], "--pitch"], input=pitch_input, text=True
).splitlines()
for case, line in zip(pitch_cases, pitch_native):
    x, y, z, pitch = case
    u.mem_write(stack + 0x3C, struct.pack("<3d", x, y, z))
    u.mem_write(stack + 0x60, struct.pack("<H", pitch))
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    u.emu_start(0x6CB605, 0x6CB747, count=100000)
    actual = struct.unpack("<3d", u.mem_read(stack + 0x3C, 24))
    expected = tuple(map(float, line.split()))
    assert actual == expected, (case, actual, expected)
assert len(pitch_native) == len(pitch_cases)

# Resolve the actual 0x6D7D20 Lobber branch, with only object access stubbed.
(
    lobber_unit,
    lobber_vtable,
    lobber_weapon,
    lobber_slot,
    lobber_target,
    lobber_target_vtable,
    lobber_pos,
) = [base + n for n in (0xA000, 0xB000, 0xC000, 0xD000, 0xE000, 0xF000, 0x10000)]
w(lobber_unit, lobber_vtable)
w(lobber_vtable + 0x39C, stop + 0x200)
w(lobber_target, lobber_target_vtable)
w(lobber_target_vtable + 0x48, stop + 0x210)


def lobber_hook(cpu, address, size, unused):
    if address not in (stop + 0x200, stop + 0x210):
        return
    sp = cpu.reg_read(UC_X86_REG_ESP)
    result = lobber_slot if address == stop + 0x200 else lobber_pos
    cpu.reg_write(UC_X86_REG_EAX, result)
    cpu.reg_write(UC_X86_REG_EIP, struct.unpack("<I", cpu.mem_read(sp, 4))[0])
    cpu.reg_write(UC_X86_REG_ESP, sp + 8)


u.hook_add(UC_HOOK_CODE, lobber_hook, begin=stop + 0x200, end=stop + 0x210)
lobber_cases = []
for _ in range(512):
    present = heading_rng.randrange(2)
    lobber = heading_rng.randrange(2)
    target_present = heading_rng.randrange(2)
    shooter = tuple(heading_rng.randrange(-5000, 5001) for _ in range(3))
    target = tuple(heading_rng.randrange(-5000, 5001) for _ in range(3))
    lobber_cases.append((present, lobber, target_present, *shooter, *target))
lobber_cases += [(1, 0, 1, 0, 0, 0, 3, 4, z) for z in (4, 5, 6)]
lobber_input = "".join(" ".join(map(str, c)) + "\n" for c in lobber_cases)
lobber_native = subprocess.check_output(
    [sys.argv[1], "--lobber"], input=lobber_input, text=True
).splitlines()
for case, line in zip(lobber_cases, lobber_native):
    present, lobber, target_present, *coords = case
    shooter = coords[:3]
    target = coords[3:]
    w(lobber_slot, lobber_weapon if present else 0)
    u.mem_write(lobber_weapon + 0x10A, bytes([lobber]))
    w(lobber_unit + 0x228, lobber_target if target_present else 0)
    u.mem_write(lobber_unit + 0x88, struct.pack("<3i", *shooter))
    u.mem_write(lobber_pos, struct.pack("<3i", *target))
    w(stack, stop)
    w(stack + 4, 0)
    u.reg_write(UC_X86_REG_ECX, lobber_unit)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    u.emu_start(0x6D7D20, stop, count=100000)
    actual = u.reg_read(UC_X86_REG_EAX) & 255
    assert actual == int(line), (case, actual, line)
assert len(lobber_native) == len(lobber_cases)

# Execute the connected normal Cannon launch path. The 0x6D7D20 branch
# is supplied as a resolved original virtual result.
launch_type, launch_bullet, launch_rules, launch_frame = (
    base + 0x70000,
    base + 0x74000,
    base + 0x78000,
    base + 0x7C000,
)
w(launch_bullet + 0x98, launch_type)
w(launch_type + 0x2AC, 0)
u.mem_write(launch_type + 0x26B, b"\1")
u.mem_write(launch_type + 0x26C, b"\0")
u.mem_write(launch_type + 0x265, b"\0")
w(0x839848, launch_rules)
launch_alternate = 0


def launch_hook(cpu, address, size, unused):
    sp = cpu.reg_read(UC_X86_REG_ESP)
    return_address = struct.unpack("<I", cpu.mem_read(sp, 4))[0]
    cpu.reg_write(UC_X86_REG_EAX, launch_alternate)
    cpu.reg_write(UC_X86_REG_ESP, sp + 8)
    cpu.reg_write(UC_X86_REG_EIP, return_address)


u.hook_add(UC_HOOK_CODE, launch_hook, begin=0x6D7D20, end=0x6D7D20)
launch_cases = []
for _ in range(256):
    dx = heading_rng.choice((-1, 1)) * heading_rng.randrange(100, 501)
    dy = heading_rng.randrange(-500, 501)
    dz = heading_rng.choice((-32, 0, 32))
    launch_cases.append((120, dx, dy, dz, 6, heading_rng.randrange(2)))
launch_input = "".join(" ".join(map(str, c)) + "\n" for c in launch_cases)
launch_native = subprocess.check_output(
    [sys.argv[1], "--tank-launch"], input=launch_input, text=True
).splitlines()
for case, line in zip(launch_cases, launch_native):
    speed, dx, dy, dz, gravity, launch_alternate = case
    w(stack + 0x7C, dx)
    w(stack + 0x80, dy)
    w(stack + 0x84, dz)
    w(stack + 0x28, speed)
    w(stack + 0x1C, launch_type)
    u.reg_write(UC_X86_REG_EAX, dy & 0xFFFFFFFF)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    u.emu_start(0x6CB062, 0x6CB15E, count=100000)
    original_speed = struct.unpack("<i", u.mem_read(stack + 0x28, 4))[0]
    original_heading = struct.unpack("<H", u.mem_read(stack + 0x20, 2))[0]
    w(stack + 0x1C, 0)
    w(stack + 0x2C, launch_bullet)
    w(launch_rules + 0x1320, gravity)
    w(launch_frame + 12, 0)
    u.reg_write(UC_X86_REG_EDI, dz & 0xFFFFFFFF)
    u.reg_write(UC_X86_REG_EBP, launch_frame)
    u.emu_start(0x6CB1AC, 0x6CB747, count=100000)
    original_pitch = struct.unpack("<H", u.mem_read(stack + 0x60, 2))[0]
    original_vector = struct.unpack("<3d", u.mem_read(stack + 0x3C, 24))
    vals = line.split()
    assert vals[0] != "none", (
        case,
        original_speed,
        original_heading,
        original_pitch,
        original_vector,
    )
    actual = (original_speed, original_heading, original_pitch, *original_vector)
    expected = (*map(int, vals[:3]), *map(float, vals[3:]))
    assert actual == expected, (case, actual, expected)
assert len(launch_native) == len(launch_cases)

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

# Entry gate before area candidate collection at 0x47E2E8.
gate_result = None


def gate_hook(cpu, address, size, unused):
    global gate_result
    gate_result = address == 0x47E321
    cpu.emu_stop()


u.hook_add(UC_HOOK_CODE, gate_hook, begin=0x47E321, end=0x47E321)
u.hook_add(UC_HOOK_CODE, gate_hook, begin=0x47F494, end=0x47F494)
gate_cases = [(damage, blocked, warhead) for damage in (-1, 0, 90)
              for blocked in (0, 1) for warhead in (0, 1)]
gate_native = subprocess.check_output(
    [sys.argv[1], "--impact-gate"],
    input="".join(" ".join(map(str, c)) + "\n" for c in gate_cases), text=True
).splitlines()
for (damage, blocked, warhead), line in zip(gate_cases, gate_native):
    gate_result = None
    w(0xA3D290, base + 0x50000)
    u.mem_write(base + 0x50000, bytes([0x20 if blocked else 0]))
    w(stack + 12, base + 0x3000 if warhead else 0)
    u.reg_write(UC_X86_REG_EBP, stack)
    u.reg_write(UC_X86_REG_ESP, stack - 0x100)
    u.reg_write(UC_X86_REG_EDI, damage & 0xFFFFFFFF)
    u.emu_start(0x47E2E8, stop, count=1000)
    assert gate_result is not None and gate_result == bool(int(line)), (damage, blocked, warhead, gate_result, line)
assert len(gate_native) == len(gate_cases)

# Final original area-damage recipient loop, after its candidate scan.
victim, victim_vtable, victim_kind, record, entries, frame = [
    base + n for n in (0x1000, 0x20000, 0x30000, 0x40000, 0x41000, 0x42000)
]
w(victim, victim_vtable)
w(victim + 0x418, victim_kind)
w(entries, record)
w(record, victim)
for offset, stub in ((0x2C, 0x300), (0x54, 0x310), (0x32C, 0x320), (0x158, 0x330)):
    w(victim_vtable + offset, stop + stub)
victim_type = victim_special = victim_half = 0
requests = []


def recipient_hook(cpu, address, size, unused):
    if address == stop + 0x300:
        return_stub(cpu, victim_type, 0)
    elif address == stop + 0x310:
        return_stub(cpu, victim_half, 0)
    elif address == stop + 0x320:
        return_stub(cpu, victim_special, 0)
    elif address == stop + 0x330:
        sp = cpu.reg_read(UC_X86_REG_ESP)
        args = struct.unpack("<6I", cpu.mem_read(sp + 4, 24))
        damage = struct.unpack("<i", cpu.mem_read(args[0], 4))[0]
        requests.append((damage, s(args[1]), args[2], args[3]))
        return_stub(cpu, 0, 6)


u.hook_add(UC_HOOK_CODE, recipient_hook, begin=stop + 0x300, end=stop + 0x330)
recipient_cases = []
for _ in range(1024):
    recipient_cases.append(
        (
            heading_rng.choice((-30, 35, 90, 120)),
            heading_rng.randrange(0, 300),
            heading_rng.randrange(0, 200),
            heading_rng.choice((1, 2, 6)),
            heading_rng.choice((-1, 0, 1, 100)),
            *(heading_rng.randrange(2) for _ in range(8)),
        )
    )
recipient_input = "".join(" ".join(map(str, c)) + "\n" for c in recipient_cases)
recipient_native = subprocess.check_output(
    [sys.argv[1], "--recipient"], input=recipient_input, text=True
).splitlines()
for case, line in zip(recipient_cases, recipient_native):
    (
        damage,
        distance,
        radius,
        victim_type,
        health,
        active,
        receives,
        limbo,
        immune,
        group,
        flag,
        victim_special,
        victim_half,
    ) = case
    u.mem_write(victim + 0x7D, bytes([active]))
    u.mem_write(victim + 0x74, bytes([receives]))
    u.mem_write(victim + 0x76, bytes([limbo]))
    u.mem_write(victim + 0x14, bytes([flag]))
    u.mem_write(victim_kind + 0x1385, bytes([immune]))
    w(victim + 0x6C, health)
    w(record + 4, distance)
    w(stack + 0x40, entries)
    w(stack + 0x24, 0)
    w(stack + 0x4C, 1)
    w(stack + 0x78, radius)
    w(stack + 0x30, damage)
    u.mem_write(stack + 0x13, bytes([group]))
    w(frame + 8, base + 0x3000)
    w(frame + 12, base + 0x2000)
    u.reg_write(UC_X86_REG_EBP, frame)
    u.reg_write(UC_X86_REG_ESP, stack)
    before = len(requests)
    u.emu_start(0x47E9DC, 0x47EAA1, count=100000)
    if line == "none":
        assert len(requests) == before, (case, requests[-1:])
    else:
        expected = tuple(map(int, line.split()))
        assert len(requests) == before + 1 and requests[-1] == (
            *expected,
            base + 0x2000,
            base + 0x3000,
        ), (case, requests[-1:], expected)
assert len(recipient_native) == len(recipient_cases)
# First candidate selection in the terrain trace. Positive is not a full hit.
trace_source, trace_dest, trace_sample, trace_ref = [base + n for n in
    (0x60000, 0x62000, 0x64000, 0x66000)]
trace_projectile, trace_rules = base + 0x68000, base + 0x6A000
trace_overlay, trace_table, trace_owner_table = base + 0x6C000, base + 0x6E000, base + 0x70000
trace_heights = {}
trace_allied = 0
trace_result = None


def trace_hook(cpu, address, size, unused):
    global trace_result
    if address == 0x47CF10:
        return_stub(cpu, trace_heights[cpu.reg_read(UC_X86_REG_ECX)], 0)
    elif address == 0x4E5540:
        return_stub(cpu, trace_allied, 1)
    else:
        trace_result = address == 0x4BBC19
        cpu.emu_stop()


for address in (0x47CF10, 0x4E5540, 0x4BBC19, 0x4BBDF8):
    u.hook_add(UC_HOOK_CODE, trace_hook, begin=address, end=address)
trace_cases = [tuple([heading_rng.randrange(2), heading_rng.randrange(2)] +
    [heading_rng.randrange(-8, 9) for _ in range(3)] +
    [heading_rng.randrange(2), heading_rng.randrange(2)] +
    [heading_rng.randrange(-4, 5), heading_rng.randrange(-4, 5)] +
    [heading_rng.randrange(2) for _ in range(3)]) for _ in range(2048)]
trace_native = subprocess.check_output([sys.argv[1], "--trace-first"],
    input="".join(" ".join(map(str, c)) + "\n" for c in trace_cases), text=True).splitlines()
for case, line in zip(trace_cases, trace_native):
    (cliffs, walls, sample_height, ref_height, source_height, same, wall,
     source_level, dest_level, rule, owner, trace_allied) = case
    trace_result = None
    trace_dest_actual = trace_sample if same else trace_dest
    trace_heights = {trace_sample: sample_height, trace_ref: ref_height,
                     trace_source: source_height}
    u.mem_write(trace_projectile + 0x266, bytes([cliffs, 0, walls]))
    u.mem_write(trace_source + 0x11B, bytes([source_level & 255]))
    u.mem_write(trace_dest_actual + 0x11B, bytes([dest_level & 255]))
    w(trace_sample + 0x44, 0)
    w(trace_sample + 0x50, 0)
    w(0xA35DEC, trace_table)
    w(trace_table, trace_overlay)
    u.mem_write(trace_overlay + 0x278, bytes([wall]))
    w(0x839848, trace_rules)
    u.mem_write(trace_rules + 0x1498, bytes([rule]))
    w(0xA3229C, trace_owner_table)
    w(trace_owner_table, base + 0x72000 if owner else 0)
    w(stack + 0x4C, trace_projectile)
    w(stack + 0x3C, trace_ref)
    w(stack + 0x10, trace_dest_actual)
    w(stack + 0x50, base + 0x74000)
    u.reg_write(UC_X86_REG_EDX, trace_projectile)
    u.reg_write(UC_X86_REG_ESI, trace_sample)
    u.reg_write(UC_X86_REG_EBX, trace_dest_actual)
    u.reg_write(UC_X86_REG_EBP, trace_source)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.emu_start(0x4BBB50, stop, count=1000)
    assert trace_result is not None and trace_result == bool(int(line)), (case, trace_result, line)
assert len(trace_native) == len(trace_cases)

print(
    f"Base game.exe: {len(speeds)} ballistic speeds, {len(angle_cases)} launch angles, {len(heading_cases)} headings, {len(velocity_cases)} horizontal vectors, {len(limit_cases)} speed limits, {len(pitch_cases)} pitch vectors, {len(lobber_cases)} Lobber branches, {len(launch_cases)} connected launches, {len(cases)} arc proposals, {len(fire_cases)} fire events, and {len(impact_cases)} impact checks, {len(recipient_cases)} damage recipients, and {len(gate_cases)} impact gates, and {len(trace_cases)} first terrain candidates match"
)
