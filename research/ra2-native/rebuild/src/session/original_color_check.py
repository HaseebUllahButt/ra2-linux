#!/usr/bin/env python3
"""Compare the native house color mapping with original x86 instructions."""
import pathlib
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / 'sim'))
from original_image import load_original

if len(sys.argv) != 3:
    raise SystemExit('usage: original_color_check.py game.exe color_cases')
exe, native = sys.argv[1:]
cpu = load_original(exe)
cpu.mem_map(0x1000000, 0x10000)
stack, end = 0x1001000, 0x100f000
entry = 0x6715d0 if pathlib.Path(exe).name.lower() == 'game.exe' else 0x69a310
lines = subprocess.check_output([native], text=True).splitlines()
for line in lines:
    value, expected = map(int, line.split())
    cpu.mem_write(stack, struct.pack('<II', end, value & 0xffffffff))
    cpu.reg_write(UC_X86_REG_ESP, stack)
    cpu.emu_start(entry, end, count=100)
    actual = cpu.reg_read(UC_X86_REG_EAX)
    actual = actual if actual < 0x80000000 else actual - 0x100000000
    assert actual == expected, (value, actual, expected)
print(f'{len(lines)} house color cases match original x86: {pathlib.Path(exe).name}')
