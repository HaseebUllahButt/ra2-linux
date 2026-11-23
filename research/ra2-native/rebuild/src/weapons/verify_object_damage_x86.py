#!/usr/bin/env python3
"""Compare base 005D3D20 through its first health write.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/{object_damage,object_damage_test}.cpp -o /tmp/ra2_object_damage
Run: RA2_GAME_DIR=/path/to/game python3 \
  rebuild/src/weapons/verify_object_damage_x86.py /tmp/ra2_object_damage
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
obj, vtable, kind, rules, damageptr = (
    base + n for n in (0x1000, 0x3000, 0x5000, 0x7000, 0x9000)
)


def w(p, v):
    u.mem_write(p, struct.pack("<I", v & 0xFFFFFFFF))


def read(p):
    return struct.unpack("<i", u.mem_read(p, 4))[0]


def ret(cpu, value, count):
    sp = cpu.reg_read(UC_X86_REG_ESP)
    cpu.reg_write(UC_X86_REG_EAX, value & 0xFFFFFFFF)
    cpu.reg_write(UC_X86_REG_EIP, struct.unpack("<I", cpu.mem_read(sp, 4))[0])
    cpu.reg_write(UC_X86_REG_ESP, sp + 4 + count * 4)


resolved = 0
observed = None


def hook(cpu, address, size, user):
    global observed
    if address == stop + 0x100:
        ret(cpu, kind, 0)
    elif address == stop + 0x110:
        ret(cpu, 1, 0)
    elif address == stop + 0x120:
        ret(cpu, 0, 0)
    elif address == 0x47E1C0:
        ret(cpu, resolved, 2)
    else:
        stage = 2 if address == 0x5D3DD4 else 3 if address == 0x5D3F39 else 0
        severity = read(cpu.reg_read(UC_X86_REG_ESP) + 0x10) if stage == 3 else 0
        observed = (stage, read(obj + 0x6C), read(damageptr), severity)
        cpu.emu_stop()


for addr in (
    stop + 0x100,
    stop + 0x110,
    stop + 0x120,
    0x47E1C0,
    0x5D3DD4,
    0x5D3DE0,
    0x5D3DB0,
    0x5D3F39,
    0x5D413A,
):
    u.hook_add(UC_HOOK_CODE, hook, begin=addr, end=addr)
w(obj, vtable)
w(vtable + 0x84, stop + 0x100)
w(vtable + 0x2C, stop + 0x110)
w(vtable + 0x80, stop + 0x100)
w(vtable + 0x32C, stop + 0x120)
w(vtable + 0x1BC, stop + 0x120)
w(0x839848, rules)
rng = random.Random(0x5D3D20)
cases = []
for _ in range(4096):
    maximum = rng.choice((1, 2, 3, 40, 100, 400, 1000))
    health = rng.randrange(maximum + 1)
    raw = rng.choice((0, 1, -1, 10, -10, maximum // 2, maximum, maximum * 2))
    resolved = rng.choice((0, 1, -1, 10, -10, maximum // 2, maximum, maximum * 2))
    bypass = rng.randrange(2)
    immune = rng.randrange(2)
    red = rng.choice((0, 0.25, 0.5, 0.75, 1))
    cases.append((health, maximum, raw, bypass, immune, red, resolved))
lines = subprocess.check_output(
    [sys.argv[1]], input="".join(" ".join(map(str, c)) + "\n" for c in cases), text=True
).splitlines()
for case, line in zip(cases, lines):
    health, maximum, raw, bypass, immune, red, resolved = case
    w(obj + 0x6C, health)
    w(kind + 0xA0, maximum)
    u.mem_write(kind + 0x203, bytes([immune]))
    u.mem_write(rules + 0x1370, struct.pack("<d", red))
    w(damageptr, raw)
    w(stack, stop)
    w(stack + 4, damageptr)
    w(stack + 8, 0)
    w(stack + 12, base + 0xB000)
    w(stack + 16, 0)
    w(stack + 20, bypass)
    w(stack + 24, 0)
    u.reg_write(UC_X86_REG_ECX, obj)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    observed = None
    u.emu_start(0x5D3D20, stop, count=100000)
    expected = tuple(map(int, line.split()))
    assert observed == expected, (case, observed, expected)
assert len(lines) == len(cases)
# Run the Unit virtual entry and both original parent handlers for bypassed
# damage. Virtual checks remain resolved stubs; no health callback is run.
chain_count = 0
for case, line in zip(cases, lines):
    health, maximum, raw, bypass, immune, red, resolved = case
    if not bypass:
        continue
    w(obj + 0x6C, health)
    w(kind + 0xA0, maximum)
    u.mem_write(kind + 0x203, bytes([immune]))
    u.mem_write(rules + 0x1370, struct.pack("<d", red))
    w(damageptr, raw)
    w(stack, stop)
    w(stack + 4, damageptr)
    w(stack + 8, 0)
    w(stack + 12, base + 0xB000)
    w(stack + 16, 0)
    w(stack + 20, bypass)
    w(stack + 24, 0)
    u.reg_write(UC_X86_REG_ECX, obj)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.reg_write(UC_X86_REG_FPCW, 0xE7F)
    observed = None
    u.emu_start(0x6FCA30, stop, count=100000)
    expected = tuple(map(int, line.split()))
    assert observed == expected, (case, observed, expected)
    chain_count += 1
print(
    f"Base game.exe: {len(cases)} shared object damage prefixes and {chain_count} Unit entry chains match"
)

# Unit's first branch after its parent damage handler returns death status 4.
death_observed = None


def death_hook(cpu, address, size, user):
    global death_observed
    if address == 0x4C6000:
        ret(cpu, 4, 6)
    else:
        branch = {0x6FCADA: 0, stop + 0x130: 1, 0x6FCD44: 2}[address]
        death_observed = (
            branch,
            read(obj + 0x5C0),
            read(obj + 0x6C),
            int(cpu.mem_read(obj + 0x7D, 1)[0] != 0),
        )
        cpu.emu_stop()


for address in (0x4C6000, 0x6FCADA, stop + 0x130, 0x6FCD44):
    u.hook_add(UC_HOOK_CODE, death_hook, begin=address, end=address)
w(vtable + 0x360, stop + 0x130)
w(obj + 0x5AC, kind)
death_cases = [
    (count, state, health, active)
    for count in (-1, 0, 1, 3)
    for state in (-2, -1, 0, 1, 7)
    for health in (0, 1, 9)
    for active in (0, 1)
]
death_lines = subprocess.check_output(
    [sys.argv[1], "--death"],
    input="".join(" ".join(map(str, c)) + "\n" for c in death_cases),
    text=True,
).splitlines()
for case, line in zip(death_cases, death_lines):
    count, state, health, active = case
    w(kind + 0xBB8, count)
    w(obj + 0x5C0, state)
    w(obj + 0x6C, health)
    u.mem_write(obj + 0x7D, bytes([active]))
    u.mem_write(obj + 0x77, b"\0")
    w(stack, stop)
    for i in range(1, 7):
        w(stack + i * 4, 0)
    u.reg_write(UC_X86_REG_ECX, obj)
    u.reg_write(UC_X86_REG_ESP, stack)
    death_observed = None
    u.emu_start(0x6FCA30, stop, count=100000)
    expected = tuple(map(int, line.split()))
    assert death_observed == expected, (case, death_observed, expected)
assert len(death_cases) == len(death_lines)
print(f"Base game.exe: {len(death_cases)} Unit death branch prefixes match")
