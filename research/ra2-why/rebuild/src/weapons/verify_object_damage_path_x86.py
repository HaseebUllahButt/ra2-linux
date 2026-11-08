#!/usr/bin/env python3
"""Compare original object damage and callback order with controlled callbacks.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/{object_damage,object_damage_path_test}.cpp -o /tmp/ra2_object_damage_path
Run: RA2_GAME_DIR=/path/to/game python3 \
  rebuild/src/weapons/verify_object_damage_path_x86.py /tmp/ra2_object_damage_path
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
obj, vtable, kind, rules, damageptr, attacker, trigger = (
    base + n for n in (0x1000, 0x3000, 0x5000, 0x7000, 0x9000, 0xB000, 0xD000)
)


def w(p, v):
    u.mem_write(p, struct.pack("<I", v & 0xFFFFFFFF))


def read(p):
    return struct.unpack("<i", u.mem_read(p, 4))[0]


def ret(cpu, value, count):
    sp = cpu.reg_read(UC_X86_REG_ESP)
    cpu.reg_write(UC_X86_REG_EAX, value & 0xFFFFFFFF)
    cpu.reg_write(UC_X86_REG_EIP, read(sp) & 0xFFFFFFFF)
    cpu.reg_write(UC_X86_REG_ESP, sp + 4 + count * 4)


events = []
mutate_at = mutation = resolved = 0


def emit(event):
    events.append(event)
    if len(events) != mutate_at:
        return
    if mutation == 1:
        u.mem_write(obj + 0x7D, b"\0")
    if mutation == 2:
        w(obj + 0x6C, 0)
    if mutation == 3:
        w(obj + 0x34, 0)
    if mutation == 4:
        w(obj + 0x6C, 1)


def hook(cpu, address, size, user):
    sp = cpu.reg_read(UC_X86_REG_ESP)
    if address == stop + 0x100:
        ret(cpu, kind, 0)
    elif address == stop + 0x110:
        ret(cpu, 1, 0)
    elif address == 0x47E1C0:
        ret(cpu, resolved, 2)
    elif address == 0x6B3DF0:
        emit((0, read(sp + 4), int(read(sp + 20) != 0)))
        ret(cpu, 0, 5)
    elif address == stop + 0x120:
        emit((1, read(sp + 4), 0))
        ret(cpu, 0, 1)
    elif address == stop + 0x130:
        emit((2, 0, int(read(sp + 4) != 0)))
        ret(cpu, 0, 1)
    elif address == stop + 0x140:
        emit((3, read(sp + 4), 0))
        ret(cpu, 0, 1)
    elif address == stop + 0x150:
        emit((4, read(sp + 4), 0))
        ret(cpu, 0, 1)


for addr in (
    stop + 0x100,
    stop + 0x110,
    0x6B3DF0,
    0x47E1C0,
    stop + 0x120,
    stop + 0x130,
    stop + 0x140,
    stop + 0x150,
):
    u.hook_add(UC_HOOK_CODE, hook, begin=addr, end=addr)
w(obj, vtable)
w(0x839848, rules)
for offset, address in (
    (0x84, stop + 0x100),
    (0x2C, stop + 0x110),
    (0x13C, stop + 0x120),
    (0xDC, stop + 0x130),
    (0xD8, stop + 0x140),
    (0x11C, stop + 0x150),
):
    w(vtable + offset, address)
rng = random.Random(0x5D3F39)
cases = []
for _ in range(4096):
    maximum = rng.choice((20, 40, 100, 400))
    health = rng.choice((1, maximum // 4, maximum // 2, maximum - 1, maximum))
    damage = rng.choice((-40, -1, 0, 1, health // 2, health, health + 1))
    red = rng.choice((0, 0.25, 0.5, 0.75, 1))
    bypass, immune = rng.randrange(2), rng.randrange(2)
    resolved = rng.choice((-40, -1, 0, 1, health // 2, health, health + 1))
    active, triggered, redraw, source = (rng.randrange(2) for _ in range(4))
    cases.append(
        (
            health,
            maximum,
            damage,
            red,
            bypass,
            immune,
            resolved,
            active,
            triggered,
            redraw,
            source,
            rng.randrange(0, 5),
            rng.randrange(5),
        )
    )
lines = subprocess.check_output(
    [sys.argv[1]], input="".join(" ".join(map(str, c)) + "\n" for c in cases), text=True
).splitlines()
action_counts = {}
status_counts = {}
for case, line in zip(cases, lines):
    (
        health,
        maximum,
        damage,
        red,
        bypass,
        immune,
        resolved,
        active,
        triggered,
        redraw,
        source,
        mutate_at,
        mutation,
    ) = case
    events = []
    w(obj + 0x6C, health)
    w(obj + 0x34, trigger if triggered else 0)
    u.mem_write(obj + 0x7D, bytes([active]))
    u.mem_write(obj + 0x77, bytes([redraw]))
    w(kind + 0xA0, maximum)
    u.mem_write(kind + 0x203, bytes([immune]))
    u.mem_write(rules + 0x1370, struct.pack("<d", red))
    w(damageptr, damage)
    w(stack, stop)
    w(stack + 4, damageptr)
    w(stack + 8, 0)
    w(stack + 12, base + 0xF000)
    w(stack + 16, attacker if source else 0)
    w(stack + 20, bypass)
    w(stack + 24, 0)
    u.reg_write(UC_X86_REG_ECX, obj)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    u.emu_start(0x5D3D20, stop, count=100000)
    actual = (
        u.reg_read(UC_X86_REG_EAX),
        read(obj + 0x6C),
        read(damageptr),
        int(u.mem_read(obj + 0x7D, 1)[0] != 0),
        int(read(obj + 0x34) != 0),
        len(events),
        *(v for event in events for v in event),
    )
    expected = tuple(map(int, line.split()))
    assert actual == expected, (case, actual, expected)
    status_counts[actual[0]] = status_counts.get(actual[0], 0) + 1
    for action in events:
        key = (action[0], action[1])
        action_counts[key] = action_counts.get(key, 0) + 1
assert len(lines) == len(cases)
for action in (
    (1, 7),
    (2, 0),
    (3, 1),
    (4, 2),
    *((0, trigger_id) for trigger_id in (6, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C)),
):
    assert action_counts.get(action, 0) > 0, (action, action_counts)
for status in (0, 1, 2, 3, 4, 5):
    assert status_counts.get(status, 0) > 0, (status, status_counts)
print(f"Base game.exe: {len(cases)} connected object damage paths match")
