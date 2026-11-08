#!/usr/bin/env python3
"""Check recovered ground coverage and corners against original x86 functions.

Requires Python unicorn and the owner's original gamemd.exe. No game code runs
outside the emulator. This checks synthetic inputs, not a complete game frame.
"""
import struct
import math
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP

GAME = Path('/path/to/user/ra2/game/gamemd.exe')
BASE, STACK, STOP = 0x1000000, 0x20fff00, 0x3000000


def setup():
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(0x400000, 0x800000)
    machine.mem_write(0x400000, GAME.read_bytes()[:0x47e000])
    machine.mem_map(BASE, 0x100000)
    machine.mem_map(0x2000000, 0x100000)
    machine.mem_map(STOP, 0x1000)
    def word(address, value):
        machine.mem_write(address, struct.pack('<I', value & 0xffffffff))
    def real(address, value):
        machine.mem_write(address, struct.pack('<f', value))
    word(BASE + 0x10, BASE + 0x100)
    word(BASE + 0x14, BASE + 0x200)
    word(BASE + 0x100, 0)
    word(BASE + 0x204, BASE + 0x400)
    machine.mem_write(BASE + 0x2a0, bytes([2, 2, 1, 2]))
    for i in range(4):
        word(BASE + 0x400 + 4 * i, 0 if i in (0, 3) else -1)
    return machine, word, real


def coverage(corners):
    machine, word, real = setup()
    entry, center = BASE + 0x500, BASE + 0x600
    for offset, x, y in corners:
        real(entry + offset, x)
        real(entry + offset + 4, y)
    for offset, value in enumerate((STOP, entry, center)):
        word(STACK + 4 * offset, value)
    machine.reg_write(UC_X86_REG_ECX, BASE)
    machine.reg_write(UC_X86_REG_ESP, STACK)
    machine.emu_start(0x756860, STOP, count=100000)
    buf = bytes(machine.mem_read(0xb2ff78, 65536))
    return {(i % 256, i // 256) for i, value in enumerate(buf) if value}


def project():
    machine, word, real = setup()
    for i, corner in enumerate(((4, 3, 1), (4, -2, 1), (-1, -2, 1), (-1, 3, 1))):
        for axis, value in enumerate(corner):
            real(BASE + 0x240 + i * 12 + axis * 4, value)
    matrix1, matrix2, offset = BASE + 0x700, BASE + 0x800, BASE + 0x900
    for address, values in ((matrix1, [1, 0, 1, 2, 0, 1, 2, 3, 0, 0, 1, 4]),
                            (matrix2, [2, 0, 0, 1, 0, 3, 0, -1, 0, 0, 1, 0])):
        for i, value in enumerate(values):
            real(address + 4 * i, value)
    for i, value in enumerate((5, 7, 0)):
        real(offset + 4 * i, value)
    for address, value in ((0xb2d5e0, 10000), (0xb2d5e4, 10000),
                           (0xb2d948, -10000), (0xb2d94c, -10000)):
        real(address, value)
    for i, value in enumerate((STOP, 0, matrix2, matrix1, offset)):
        word(STACK + 4 * i, value)
    machine.reg_write(UC_X86_REG_ECX, BASE)
    machine.reg_write(UC_X86_REG_EDX, 0)
    machine.reg_write(UC_X86_REG_ESP, STACK)
    machine.emu_start(0x753f90, STOP, count=100000)
    return [struct.unpack('<3f', machine.mem_read(0xb3ff84 + 12 * i, 12)) for i in range(4)]


cases = [
    ([(0x24, 0, 0), (0x18, 4, 0), (0x30, 0, 4)],
     {(128, 128), (129, 128), (130, 130), (131, 130)}),
    ([(0x24, 3.5, -2), (0x18, -1.5, 1), (0x30, 5.5, 5)],
     {(131, 126), (132, 126), (130, 131), (131, 131)}),
    ([(0x24, 127, 0), (0x18, 129, 0), (0x30, 127, 2)],
     {(255, 128), (0, 129), (1, 129)}),
]
for corners, expected in cases:
    assert coverage(corners) == expected
assert project() == [(20, -30, 0), (20, -15, 0), (10, -15, 0), (10, -30, 0)]
print('Original x86: three coverage cases and one ground projection case passed')

# Run original matrix rotations with explicit sine and cosine values.
machine, word, real = setup()
matrix = BASE + 0xa00
for i, value in enumerate((1,0,0,0,0,1,0,0,0,0,1,0)):
    real(matrix + i*4, value)
for function, angle in ((0x5af000, -math.pi/3), (0x5af240, -math.pi/4)):
    word(STACK, STOP)
    real(STACK+4, math.sin(angle))
    real(STACK+8, math.cos(angle))
    machine.reg_write(UC_X86_REG_ECX, matrix)
    machine.reg_write(UC_X86_REG_ESP, STACK)
    machine.emu_start(function, STOP, count=100000)
actual = struct.unpack('<12f', machine.mem_read(matrix, 48))
expected_camera = (.70710678,.70710678,0,0,-.35355339,.35355339,.8660254,0,.61237244,-.61237244,.5,0)
assert all(abs(a-b) < 0.000001 for a,b in zip(actual, expected_camera))
print('Original x86: default camera matrix passed')

# 0x43E085 rotates a 32-step facing and translates by integer offset / 8.
machine, word, real = setup()
for i, value in enumerate((1,0,0,0,0,1,0,0,0,0,1,0)):
    real(matrix + i*4, value)
word(STACK, STOP)
real(STACK+4, math.sin(math.pi/2))
real(STACK+8, math.cos(math.pi/2))
machine.reg_write(UC_X86_REG_ECX, matrix)
machine.reg_write(UC_X86_REG_ESP, STACK)
machine.emu_start(0x5af240, STOP, count=100000)
word(STACK, STOP)
real(STACK+4, 2)
machine.reg_write(UC_X86_REG_ECX, matrix)
machine.reg_write(UC_X86_REG_ESP, STACK)
machine.emu_start(0x5ae980, STOP, count=100000)
facing = struct.unpack('<12f', machine.mem_read(matrix,48))
assert abs(facing[0]) < .000001 and abs(facing[4]-1) < .000001
assert abs(facing[3]) < .000001 and abs(facing[7]-2) < .000001
print('Original x86: facing rotation and offset passed')

# The barrel path translates by minus the stored pivot, rotates, then restores it.
machine, word, real = setup()
for i, value in enumerate((0,-1,0,0,1,0,0,2,0,0,1,0)):
    real(matrix + i*4, value)
vector = BASE + 0xb00
for sign, steps in ((-1, (0x5af240, 0x5af120)), (1, ())):
    for i, value in enumerate((0,sign*2,0)):
        real(vector+i*4, value)
    word(STACK, STOP)
    word(STACK+4, vector)
    machine.reg_write(UC_X86_REG_ECX, matrix)
    machine.reg_write(UC_X86_REG_ESP, STACK)
    machine.emu_start(0x5ae8f0, STOP, count=100000)
    for function in steps:
        angle = math.pi/2 if function == 0x5af240 else -math.pi/2
        word(STACK, STOP)
        real(STACK+4, math.sin(angle))
        real(STACK+8, math.cos(angle))
        machine.reg_write(UC_X86_REG_ECX, matrix)
        machine.reg_write(UC_X86_REG_ESP, STACK)
        machine.emu_start(function, STOP, count=100000)
barrel = struct.unpack('<12f', machine.mem_read(matrix,48))
expected_barrel = (0,0,1,2,0,-1,0,0,1,0,0,0)
assert all(abs(a-b)<.00001 for a,b in zip(barrel,expected_barrel)), barrel
print('Original x86: barrel pivot rotation passed')
