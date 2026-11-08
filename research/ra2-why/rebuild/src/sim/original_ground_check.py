#!/usr/bin/env python3
"""Check 004B4780 geometry with original x86 instructions. Requires Unicorn.
Usage: original_ground_check.py /path/to/gamemd.exe [--write]
--write refreshes ground_golden.inc in this script's directory.
"""
import pathlib
import struct
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP

image = pathlib.Path(sys.argv[1]).read_bytes()
cpu = Uc(UC_ARCH_X86, UC_MODE_32)
cpu.mem_map(0x400000, 0x500000)
cpu.mem_write(0x400000, image[:0x500000])
cpu.mem_map(0x1000000, 0x20000)
rows = []
for track in range(64):
    cpu.mem_write(0x1000040, struct.pack('<iii', 100, 200, 300))
    cpu.mem_write(0x1000058, struct.pack('<i', track))
    cpu.mem_write(0x1001000, struct.pack('<iii', 17, -9, 42))
    cpu.mem_write(0x1002000, struct.pack('<I', 23))
    cpu.mem_write(0x1003000, struct.pack('<iii', 0, 0, 999))
    cpu.reg_write(UC_X86_REG_ECX, 0x1000000)
    cpu.reg_write(UC_X86_REG_ESP, 0x101f000)
    cpu.mem_write(0x101f000, struct.pack('<4I', 0x101e000, 0x1003000, 0x1001000, 0x1002000))
    cpu.emu_start(0x4b4780, 0x101e000, count=10000)
    x, y, z = struct.unpack('<iii', cpu.mem_read(0x1003000, 12))
    angle = struct.unpack('<I', cpu.mem_read(0x1002000, 4))[0]
    assert z == 999  # Original routine leaves output z untouched.
    rows.append('{' + ','.join(map(str, (x, y, angle))) + '},\n')
content = '// Golden 004B4780 x86 results: track row -> x, y, facing.\n' + ''.join(rows)
fixture = pathlib.Path(__file__).with_name('ground_golden.inc')
if '--write' in sys.argv:
    fixture.write_text(content)
else:
    assert fixture.read_text() == content
print('64 original x86 ground curve results match native test vectors')
