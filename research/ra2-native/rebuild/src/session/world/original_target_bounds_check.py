#!/usr/bin/env python3
"""Compare the base bounds getter with the conditional MP02T2 start tile.

The original getter runs with a supplied virtual TMP-header getter and record
pointer. The values come from the native MIX and TMP readers. This does not
execute Overlay, object, bridge, Tube, animation, shadow, or Cell setup phases.
"""
import struct
import subprocess
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP
from original_image import load_original

if len(sys.argv) != 4:
    raise SystemExit('usage: original_target_bounds_check.py game.exe target_tile_case game_dir')
width, height, flags, y, extra_y = map(int, subprocess.check_output(
    [sys.argv[2], sys.argv[3], '--bounds'], text=True).split())
assert width == 60 and height == 30 and not (flags & 1)

cpu = load_original(sys.argv[1])
cpu.mem_map(0x1000000, 0x10000)
obj, vt, header, record = 0x1000000, 0x1001000, 0x1002000, 0x1003000
getter, output, stack, stop = 0x1004000, 0x1005000, 0x100e000, 0x100f000

def word(address, value):
    cpu.mem_write(address, struct.pack('<I', value & 0xffffffff))

word(obj, vt)
word(vt + 0x9c, getter)
for offset, value in ((0, 1), (4, 1), (8, width), (12, height), (16, record)):
    word(header + offset, value)
word(record + 4, y)
word(record + 0x18, extra_y)
word(record + 0x24, flags)
cpu.mem_write(getter, b'\xc3')

def hook(cpu, address, size, user):
    if address == getter:
        cpu.reg_write(UC_X86_REG_EAX, header)

cpu.hook_add(UC_HOOK_CODE, hook)
cpu.mem_write(stack, struct.pack('<IIII', stop, 0, output, output + 4))
cpu.reg_write(UC_X86_REG_ECX, obj)
cpu.reg_write(UC_X86_REG_ESP, stack)
cpu.emu_start(0x52b690, stop, count=10000)
assert cpu.reg_read(UC_X86_REG_EAX) & 0xff == 1
assert struct.unpack('<ii', cpu.mem_read(output, 8)) == (width, height)
assert cpu.reg_read(UC_X86_REG_ESP) == stack + 16
print('base x86 MP02T2 conditional start-tile bounds are 60 by 30')
