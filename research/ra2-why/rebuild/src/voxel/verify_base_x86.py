#!/usr/bin/env python3
"""Verify base RA2 camera and facing from its original x86 code via Unicorn."""

import math
import struct
import re
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_ECX,
    UC_X86_REG_ESP,
)

exe = Path("/path/to/user/ra2/game/game.exe").read_bytes()
machine = Uc(UC_ARCH_X86, UC_MODE_32)
machine.mem_map(0x400000, 0x800000)
machine.mem_write(0x400000, exe[:0x431000])
machine.mem_map(0x1000000, 0x100000)
machine.mem_map(0x2000000, 0x100000)
machine.mem_map(0x3000000, 0x1000)
stack, stop, base = 0x20FFF00, 0x3000000, 0x1000000


def word(address, value):
    machine.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))


def real(address, value):
    machine.mem_write(address, struct.pack("<f", value))


def call(address):
    word(stack, stop)
    machine.reg_write(UC_X86_REG_ESP, stack)
    machine.emu_start(address, stop, count=100000)


# These original initializers set the 60-degree X and 45-degree Z camera angles.
call(0x717D70)
call(0x717D50)
assert abs(struct.unpack("<d", machine.mem_read(0xAF7390, 8))[0] - math.pi / 3) < 1e-12
assert abs(struct.unpack("<d", machine.mem_read(0xAF7928, 8))[0] - math.pi / 4) < 1e-12
# Run the camera setup itself, through its two original trigonometric helpers.
machine.reg_write(UC_X86_REG_ESP, stack)
machine.emu_start(0x718C7E, 0x718CB4, count=1000000)
camera = struct.unpack("<12f", machine.mem_read(0xAF77A8, 48))
expected = (
    0.7081906199455261,
    0.706021249294281,
    0,
    0,
    -0.3531669080257416,
    0.35425207018852234,
    0.865897536277771,
    0,
    0.6113420128822327,
    -0.6132205128669739,
    0.5002213716506958,
    0,
)
assert all(a == b for a, b in zip(camera, expected)), camera

# Execute the original facing quantization, Z rotation and turret offset sequence.
source = Path(__file__).with_name("base_facing.inc").read_text()
values = re.findall(
    r"\{\{(-?0x[0-9a-f.]+p[+-][0-9]+)f, (-?0x[0-9a-f.]+p[+-][0-9]+)f\}\}", source
)
assert len(values) == 32
for facing in range(0, 65536, 2048):
    for i, value in enumerate((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0)):
        real(stack + 0x58 + i * 4, value)
    word(base, facing)
    word(base + 0x100 + 0x418, base + 0x600)
    word(base + 0x600 + 0x63C, 17)
    machine.reg_write(UC_X86_REG_EAX, base)
    machine.reg_write(UC_X86_REG_EBP, base + 0x100)
    machine.reg_write(UC_X86_REG_ESP, stack)
    machine.emu_start(0x43AF99, 0x43AFEC, count=1000000)
    matrix = struct.unpack("<12f", machine.mem_read(stack + 0x58, 48))
    step = (((facing >> 10) + 1) >> 1) & 31
    angle = (step - 8) * (-math.pi / 16)
    c, s = map(float.fromhex, values[step])
    expected = (c, -s, 0, 2 * c, s, c, 0, 2 * s, 0, 0, 1, 0)
    assert all(abs(a - b) < 1e-7 for a, b in zip(matrix, expected)), (
        facing,
        matrix,
        expected,
    )
print("Base game.exe: camera and 32 original facing/offset cases passed")

# Execute the base game's barrel tilt sequence for all 32 facing values.
tilt_source = Path(__file__).with_name("base_tilt.inc").read_text()
tilt_values = re.findall(
    r"\{\{(-?0x[0-9a-f.]+p[+-][0-9]+)f, (-?0x[0-9a-f.]+p[+-][0-9]+)f\}\}", tilt_source
)
assert len(tilt_values) == 32
for facing in range(0, 65536, 2048):
    for i, value in enumerate((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0)):
        real(stack + 0x58 + i * 4, value)
    word(base, facing)
    machine.reg_write(UC_X86_REG_EAX, base)
    machine.reg_write(UC_X86_REG_ESP, stack)
    machine.emu_start(0x43B3EA, 0x43B415, count=1000000)
    matrix = struct.unpack("<12f", machine.mem_read(stack + 0x58, 48))
    step = (((facing >> 10) + 1) >> 1) & 31
    c, s = map(float.fromhex, tilt_values[step])
    assert abs(matrix[0] - c) < 1e-7 and abs(matrix[2] - s) < 1e-7
print("Base game.exe: 32 original barrel tilt cases passed")

# Execute the caller through turret and barrel matrix setup. Replace only the
# facing-object accessor; all matrix, angle, and offset code remains original.
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESI

obj, kind, face_pointer = base + 0x100, base + 0x600, base + 0xE00
word(obj + 0x418, kind)
word(kind + 0xB8, 1)
word(kind + 0x63C, 16)
for i, value in enumerate((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0)):
    real(stack + 0x58 + i * 4, value)

face_values = {obj + 0x2E0: 0x8000, obj + 0x2C8: 0x4000}


def facing_hook(cpu, address, size, unused):
    current = cpu.reg_read(UC_X86_REG_ECX)
    word(face_pointer, face_values.get(current, 0x4000))
    sp = cpu.reg_read(UC_X86_REG_ESP)
    ret = struct.unpack("<I", cpu.mem_read(sp, 4))[0]
    cpu.reg_write(UC_X86_REG_EAX, face_pointer)
    cpu.reg_write(UC_X86_REG_ESP, sp + 8)
    cpu.reg_write(UC_X86_REG_EIP, ret)


machine.hook_add(UC_HOOK_CODE, facing_hook, begin=0x4B8BE0, end=0x4B8BE0)
machine.reg_write(UC_X86_REG_EBP, obj)
machine.reg_write(UC_X86_REG_ESP, stack)
machine.emu_start(0x43AF89, 0x43B10E, count=1000000)
turret = struct.unpack("<12f", machine.mem_read(stack + 0x58, 48))
barrel = struct.unpack("<12f", machine.mem_read(stack + 0x88, 48))
expected_turret = (
    0.0015339801320806146,
    0.9999988079071045,
    0,
    0.003067960264161229,
    -0.9999988079071045,
    0.0015339801320806146,
    0,
    -1.999997615814209,
    0,
    0,
    1,
    0,
)
expected_barrel = (
    0.0015339801320806146,
    0.9999988079071045,
    0,
    0.003067731624469161,
    -0.9999988079071045,
    0.0015339801320806146,
    0,
    -1.9999973773956299,
    0,
    0,
    1,
    0,
)
assert turret == expected_turret, turret
assert barrel == expected_barrel, barrel
print("Base game.exe: turret and barrel caller matrices passed")


# 0x43B14A draws the barrel before the turret for quadrants 0 and 3.
def order_hook(cpu, address, size, unused):
    cpu.emu_stop()


machine.hook_add(UC_HOOK_CODE, order_hook, begin=0x43B17C, end=0x43B17C)
machine.hook_add(UC_HOOK_CODE, order_hook, begin=0x43B203, end=0x43B203)
for facing in range(0, 65536, 2048):
    face_values[obj + 0x2E0] = facing
    machine.reg_write(UC_X86_REG_ESI, obj + 0x2E0)
    machine.reg_write(UC_X86_REG_ESP, stack)
    machine.emu_start(0x43B142, 0x43B30E, count=1000000)
    quadrant = (((facing >> 13) + 1) >> 1) & 3
    expected_address = 0x43B17C if quadrant in (0, 3) else 0x43B203
    assert machine.reg_read(UC_X86_REG_EIP) == expected_address
print("Base game.exe: barrel/turret order passed for 32 facings")

# Compare a turned turret and raised barrel with a negative turret offset.
word(kind + 0x63C, -16)
face_values[obj + 0x2E0] = 0x2000
face_values[obj + 0x2C8] = 0
for i, value in enumerate((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0)):
    real(stack + 0x58 + i * 4, value)
machine.reg_write(UC_X86_REG_EBP, obj)
machine.reg_write(UC_X86_REG_ESP, stack)
machine.emu_start(0x43AF89, 0x43B10E, count=1000000)
tilted = struct.unpack("<12f", machine.mem_read(stack + 0x88, 48))
expected_tilted = (
    0.0010846876539289951,
    -0.7071067690849304,
    -0.7071058750152588,
    -0.4157473146915436,
    0.0010846876539289951,
    0.7071067690849304,
    -0.7071058750152588,
    -0.41574758291244507,
    0.9999988079071045,
    0,
    0.0015339801320806146,
    -1.4142117500305176,
)
assert tilted == expected_tilted, tilted
print("Base game.exe: turned turret and raised barrel matrices passed")

# Run the original base-game vehicle lighting setup and compare all table bytes.
word(stack, stop)
real(stack + 4, 0.7853749990463257)
machine.reg_write(UC_X86_REG_ESP, stack)
machine.emu_start(0x717FB0, stop, count=1000000)
assert struct.unpack("<3f", machine.mem_read(0x839AB0, 12)) == (
    -0.49999943375587463,
    -0.7071059942245483,
    0.49999943375587463,
)
light_identity = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0)
light_camera = (
    0.7081906199455261,
    0.706021249294281,
    0,
    0,
    -0.3531669080257416,
    0.35425207018852234,
    0.865897536277771,
    0,
    0.6113420128822327,
    -0.6132205128669739,
    0.5002213716506958,
    0,
)
light_expected = (
    (0xA63457AC98D80794, 0x64F4499B06F451EC),
    (0x44480F1599DCDA35, 0xF4C3D01914ADBEA1),
)
for case, transform in enumerate((light_identity, light_camera)):
    for normal_index, normal_type in enumerate((2, 4)):
        machine.mem_write(0xAF8E10, bytes(256))
        for addr, values in ((base + 0x2000, transform), (0x839A70, light_identity)):
            for i, value in enumerate(values):
                real(addr + i * 4, value)
        word(base + 0x10, base + 0x100)
        word(base + 0x14, base + 0x200)
        word(base + 0x100, 0)
        machine.mem_write(base + 0x2A3, bytes([normal_type]))
        for i, value in enumerate((stop, 0, base + 0x2000, 0x839A70, 0x839AB0)):
            word(stack + i * 4, value)
        real(stack + 20, 3)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, base)
        from unicorn.x86_const import UC_X86_REG_EDX

        machine.reg_write(UC_X86_REG_EDX, 0)
        machine.emu_start(0x7170D0, stop, count=1000000)
        table = machine.mem_read(0xAF8E10, 256)
        digest = 14695981039346656037
        for value in table:
            digest = ((digest ^ value) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
        assert digest == light_expected[case][normal_index], (
            case,
            normal_type,
            hex(digest),
        )
print("Base game.exe: four original vehicle lighting tables passed")

# The map-placement paths initialize the turret, barrel and hull facing fields.
from unicorn.x86_const import UC_X86_REG_ESI, UC_X86_REG_EBX
for facing in (0, 1, 0x72, 0xff):
    actor = base + 0x5000
    machine.mem_write(actor, bytes(0x400))
    machine.reg_write(UC_X86_REG_ESI, actor)
    machine.reg_write(UC_X86_REG_EBX, facing)
    machine.reg_write(UC_X86_REG_ESP, stack)
    machine.emu_start(0x6c4700, 0x6c4732, count=100000)
    assert struct.unpack('<H', machine.mem_read(actor+0x2e0,2))[0] == facing*256
    assert struct.unpack('<H', machine.mem_read(actor+0x2c8,2))[0] == 0x4000
    machine.reg_write(UC_X86_REG_ESI, actor)
    machine.reg_write(UC_X86_REG_EBX, facing)
    machine.reg_write(UC_X86_REG_ESP, stack)
    machine.emu_start(0x6fc95e, 0x6fc977, count=100000)
    assert struct.unpack('<H', machine.mem_read(actor+0x2f8,2))[0] == facing*256
print('Base game.exe: initial hull, turret, and barrel facing passed')
