#!/usr/bin/env python3
"""Check recovered ground coverage and corners against original x86 functions.

Requires Python unicorn and the owner's original gamemd.exe. No game code runs
outside the emulator. This checks synthetic inputs, not a complete game frame.
"""
import struct
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
