#!/usr/bin/env python3
"""Check native damage vectors against 00489180 x86 instructions. Requires Unicorn.
Usage: original_damage_check.py /path/to/gamemd.exe
Inputs use TankOGas's authored .5 CellSpread, .9 PercentAtMax, and 25% armor verse.
"""
import pathlib
import struct
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_FPCW

image = pathlib.Path(sys.argv[1]).read_bytes()
base = '--base' in sys.argv
routine = 0x47e1c0 if base else 0x489180
scenario_pointer = 0xa3d290 if base else 0xa8b230
rules_pointer = 0x839848 if base else 0x8871e0
maximum_offset = 0x1330 if base else 0x16c8
cpu = Uc(UC_ARCH_X86, UC_MODE_32)
cpu.mem_map(0x400000, 0x500000)
cpu.mem_write(0x400000, image[:0x500000])
cpu.reg_write(UC_X86_REG_FPCW, 0x0e7f)
cpu.mem_map(0xa00000, 0x100000)
cpu.mem_map(0x1000000, 0x20000)
cpu.mem_write(scenario_pointer, struct.pack('<I', 0x1004000))
cpu.mem_write(rules_pointer, struct.pack('<I', 0x1005000))
cpu.mem_write(0x1005000 + maximum_offset, struct.pack('<i', 10000))
cpu.mem_write(0x1000000 + 0x124, struct.pack('<f', .5))
cpu.mem_write(0x1000000 + 0x12c, struct.pack('<f', .9))
cpu.mem_write(0x1000000 + 0xa0, struct.pack('<d', .25))
distances = (0, 7, 8, 16, 64, 128, 256)
cases = (
    (90, (22, 22, 22, 22, 21, 20, 17)),
    (120, (30, 29, 29, 29, 28, 26, 23)),
    (10000, (2500, 2486, 2484, 2468, 2374, 2249, 1999)),
    (-50, (-50, -50, 0, 0, 0, 0, 0)),
)
for damage, expected in cases:
    for distance, golden in zip(distances, expected):
        cpu.reg_write(UC_X86_REG_ECX, damage & 0xffffffff)
        cpu.reg_write(UC_X86_REG_EDX, 0x1000000)
        cpu.reg_write(UC_X86_REG_ESP, 0x101f000)
        cpu.mem_write(0x101f000, struct.pack('<III', 0x101e000, 0, distance))
        cpu.emu_start(routine, 0x101e000, count=10000)
        actual = cpu.reg_read(UC_X86_REG_EAX)
        if actual >= 0x80000000:
            actual -= 0x100000000
        assert actual == golden, (damage, distance, actual, golden)
print('28 original x86 damage results match native test vectors')

replay_hash = 2166136261
count = 0
for damage in (90, 120, 10000):
    for spread in (0., .1, .3, .5, .75, 1.5):
        cpu.mem_write(0x1000000 + 0x124, struct.pack('<f', spread))
        for percent in (0., .1, .5, .9, 1., 2.):
            cpu.mem_write(0x1000000 + 0x12c, struct.pack('<f', percent))
            for verse in (0., .1, .25, .9, 1., 2.):
                cpu.mem_write(0x1000000 + 0xa0, struct.pack('<d', verse))
                for distance in (0, 1, 7, 8, 16, 64, 128, 256, 1024):
                    cpu.reg_write(UC_X86_REG_ECX, damage)
                    cpu.reg_write(UC_X86_REG_EDX, 0x1000000)
                    cpu.reg_write(UC_X86_REG_ESP, 0x101f000)
                    cpu.mem_write(0x101f000, struct.pack('<III', 0x101e000, 0, distance))
                    cpu.emu_start(routine, 0x101e000, count=10000)
                    actual = cpu.reg_read(UC_X86_REG_EAX)
                    replay_hash = ((replay_hash ^ actual) * 16777619) & 0xffffffff
                    count += 1
assert count == 5832 and replay_hash == 0xaff8a2e1
print(f'{count} original damage grid cases produce hash {replay_hash:08x}')
