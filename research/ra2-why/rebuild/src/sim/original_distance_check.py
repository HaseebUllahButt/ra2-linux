#!/usr/bin/env python3
"""Check original lookup-square-root output bits. Requires Unicorn.
Usage: original_distance_check.py /path/to/gamemd.exe [--base]
For --base, pass game.exe.
"""
import pathlib
import struct
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_FPCW
image = pathlib.Path(sys.argv[1]).read_bytes()
helper = 0x4ba410 if '--base' in sys.argv else 0x4cac40
cpu = Uc(UC_ARCH_X86, UC_MODE_32)
cpu.mem_map(0x400000, 0x500000)
cpu.mem_write(0x400000, image[:0x500000])
cpu.mem_map(0x1000000, 0x20000)
cpu.reg_write(UC_X86_REG_FPCW, 0xe7f)
cpu.mem_write(0x101e000, b'\xd9\x1d' + struct.pack('<I', 0x1000000))
cases = [(0, 0), (1, 0x3f800000), (2, 0x3fb504f3), (3, 0x3fddb3d7),
         (25, 0x40a00000), (10000, 0x42c80000), (3211264, 0x44e00000),
         (123456789, 0x462d9bd7), (68719476736, 0x48800000)]
for squared, expected in cases:
    cpu.reg_write(UC_X86_REG_ESP, 0x101f000)
    cpu.mem_write(0x101f000, struct.pack('<Id', 0x101e000, squared))
    cpu.emu_start(helper, 0x101e006, count=10000)
    actual = struct.unpack('<I', cpu.mem_read(0x1000000, 4))[0]
    assert actual == expected, (squared, actual, expected)
print('9 original lookup-square-root outputs match native float bits')
