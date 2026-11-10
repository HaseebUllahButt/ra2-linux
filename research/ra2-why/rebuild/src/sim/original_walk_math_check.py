#!/usr/bin/env python3
"""Check the ported original walk step against the original executable.

Emulates the original Walk Internal_Process step 0071F2FA..0071F420 from
game.exe with Unicorn. The Foot virtual calls are fake stubs: 0x48 returns
the current coordinate, 0x4BC the integer speed, 0x4C8 stops the timer,
0x1A0 returns the current cell, and the 0x4C facing setter captures the
facing. Everything between, including 004BA600 (arctangent, table
00813DF4), 004BA480/004BA4D0 (sine/cosine, table 00801DC4) and 0077F110
(truncation), is original code.

Usage: original_walk_math_check.py /path/to/game.exe [printed-output-file]
"""
import pathlib
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_EBX, \
    UC_X86_REG_EAX, UC_X86_REG_ESI, UC_X86_REG_FPCW

STEP_BEGIN = 0x71F2FA
STEP_END = 0x71F420

# (current, destination, speed) vectors. Axial, all eight diagonal octants,
# off-axis slopes, near target overshoot, speed 0, speed 1, large and
# negative coordinates, and the no-distance case.
VECTORS = [
    ((1000, 2000, 0), (2000, 2000, 0), 100),
    ((2000, 2000, 0), (1000, 2000, 0), 100),
    ((2000, 2000, 0), (2000, 1000, 0), 100),
    ((2000, 1000, 0), (2000, 2000, 0), 100),
    ((1000, 2000, 0), (2000, 1000, 0), 141),
    ((2000, 2000, 0), (1000, 1000, 0), 141),
    ((1000, 1000, 0), (2000, 2000, 0), 141),
    ((2000, 1000, 0), (1000, 2000, 0), 141),
    ((5000, 5000, 3), (5300, 5100, 3), 50),
    ((5000, 5000, 3), (5100, 5300, 3), 50),
    ((5000, 5000, 3), (4700, 5100, 3), 50),
    ((5000, 5000, 3), (4900, 5300, 3), 50),
    ((5000, 5000, 3), (5300, 4900, 3), 50),
    ((5000, 5000, 3), (4700, 4900, 3), 50),
    ((5000, 5000, 3), (5100, 4700, 3), 50),
    ((5000, 5000, 3), (4900, 4700, 3), 50),
    ((1000, 1000, 0), (1010, 1000, 0), 100),
    ((1000, 1000, 0), (1006, 1008, 0), 100),
    ((1000, 2000, 7), (5000, 9000, 7), 0),
    ((0, 0, 0), (1000, 1, 0), 1),
    ((100000, 200000, 0), (200000, 100000, 0), 500),
    ((-5000, -5000, 0), (-8000, -2000, 0), 200),
    ((1234, 4321, 9), (1234, 4321, 9), 100),
    ((0, 0, 0), (0, -10000, 0), 300),
    ((0, 0, 0), (-10000, 0, 0), 300),
    ((0, 0, 0), (0, 10000, 0), 300),
    ((0, 0, 0), (3, 1, 0), 10),
    ((0, 0, 0), (1, 3, 0), 10),
    ((0, 0, 0), (-1, 3, 0), 10),
    ((0, 0, 0), (10000, 250, 0), 300),
    ((0, 0, 0), (-250, -1000, 0), 300),
    ((7000, 7000, 1), (6990, 7010, 1), 25),
]

SCRATCH = 0x1000000
WALK = 0x1001000       # walk locomotor object: +0x28 destination, +0x88 position
FOOT = 0x1002000       # Foot object, vtable at +0
FACING_OBJECT = 0x1004100
VTABLE = 0x1003000
VTABLE2 = 0x1003100
STUBS = 0x1004000
FOOT_COORD = 0x1005000
CELL_COORD = 0x1005100
SPEED = 0x1005300
CAPTURE = 0x1005200
STACK = 0x101F000
FRAME = 0x101E100


class OriginalStep:
    def __init__(self, path):
        image = pathlib.Path(path).read_bytes()
        self.cpu = Uc(UC_ARCH_X86, UC_MODE_32)
        self.cpu.mem_map(0x400000, 0x500000)
        self.cpu.mem_write(0x400000, image[:0x500000])
        self.cpu.mem_map(SCRATCH, 0x20000)
        self.cpu.reg_write(UC_X86_REG_FPCW, 0xE7F)
        self.cpu.mem_write(FRAME + 4, struct.pack('<I', VTABLE2))
        self.cpu.mem_write(FRAME + 0xC, struct.pack('<I', FOOT))
        self.cpu.mem_write(FOOT, struct.pack('<I', VTABLE))
        # Stub bodies. Each vtable slot holds the stub address.
        # 0x48: return the foot coordinate pointer, stdcall one argument.
        self.cpu.mem_write(STUBS + 0x00, b'\xb8' + struct.pack('<I', FOOT_COORD) + b'\xc2\x04\x00')
        # 0x1A0: return the current cell pointer, stdcall one argument.
        self.cpu.mem_write(STUBS + 0x10, b'\xb8' + struct.pack('<I', CELL_COORD) + b'\xc2\x04\x00')
        # 0x4C8: stop movement sound timer, stdcall two arguments.
        self.cpu.mem_write(STUBS + 0x20, b'\xc2\x08\x00')
        # 0x4BC: return the integer speed in eax, no stacked arguments.
        # The stub reads the speed from memory so no code changes per call.
        self.cpu.mem_write(STUBS + 0x30, b'\xa1' + struct.pack('<I', SPEED) + b'\xc3')
        # 0x4C: capture the facing argument (second stack slot), stdcall.
        self.cpu.mem_write(STUBS + 0x40,
                           b'\x8b\x44\x24\x08\xa3' + struct.pack('<I', CAPTURE) + b'\xc2\x08\x00')
        self.cpu.mem_write(VTABLE + 0x48, struct.pack('<I', STUBS + 0x00))
        self.cpu.mem_write(VTABLE + 0x1A0, struct.pack('<I', STUBS + 0x10))
        self.cpu.mem_write(VTABLE + 0x4C8, struct.pack('<I', STUBS + 0x20))
        self.cpu.mem_write(VTABLE + 0x4BC, struct.pack('<I', STUBS + 0x30))
        self.cpu.mem_write(VTABLE2 + 0x4C, struct.pack('<I', STUBS + 0x40))
        self.cpu.mem_write(CELL_COORD, bytes(16))

    def step(self, current, destination, speed):
        self.cpu.mem_write(WALK + 0x28, struct.pack('<iii', *destination))
        # The step block reads the position from Foot+0x88 (0071F39E) and the
        # destination from walk+0x28 (EBX, cleared at 0071F294).
        self.cpu.mem_write(FOOT + 0x88, struct.pack('<iii', *current))
        # vtable 0x48 returns this coordinate pointer (the angle source).
        self.cpu.mem_write(FOOT_COORD, struct.pack('<ii', current[0], current[1]))
        self.cpu.mem_write(SPEED, struct.pack('<i', speed))
        self.cpu.mem_write(CAPTURE, bytes(4))
        self.cpu.reg_write(UC_X86_REG_ESP, STACK)
        self.cpu.reg_write(UC_X86_REG_EBP, FRAME)
        self.cpu.reg_write(UC_X86_REG_EBX, WALK + 0x28)
        self.cpu.emu_start(STEP_BEGIN, STEP_END, count=100000)
        new_x = struct.unpack('<i', struct.pack('<I', self.cpu.reg_read(UC_X86_REG_EAX)))[0]
        new_y = struct.unpack('<i', struct.pack('<I', self.cpu.reg_read(UC_X86_REG_ESI)))[0]
        raw_facing = struct.unpack('<I', self.cpu.mem_read(CAPTURE, 4))[0] & 0xFFFF
        facing = raw_facing - 0x10000 if raw_facing >= 0x8000 else raw_facing
        return (new_x, new_y, current[2]), facing


def main():
    printed = None
    if len(sys.argv) > 2:
        printed = pathlib.Path(sys.argv[2]).read_text().splitlines()
    original = OriginalStep(sys.argv[1])
    mismatches = 0
    for index, vector in enumerate(VECTORS):
        position, facing = original.step(vector[0], vector[1], vector[2])
        line = f'{index} {position[0]} {position[1]} {position[2]} {facing}'
        if printed is not None:
            if printed[index] != line:
                print(f'vector {index}: port {printed[index]} original {line}')
                mismatches += 1
        else:
            print(line)
    if printed is not None:
        if mismatches:
            raise SystemExit(f'{mismatches} ported walk step outputs differ from the original')
        print(f'{len(VECTORS)} ported walk step outputs match the original executable')
    else:
        print(f'{len(VECTORS)} original walk step vectors generated')


if __name__ == '__main__':
    main()
