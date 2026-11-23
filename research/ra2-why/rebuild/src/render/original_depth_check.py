#!/usr/bin/env python3
"""Compare native terrain depth with both original executables through Unicorn.

Run: original_depth_check.py GAME_DIRECTORY NATIVE_DEPTH_TEST
This reads owned files and runs only the recovered depth arithmetic.
"""
from pathlib import Path
import random
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sim"))
from original_image import load_original
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_ESP, UC_X86_REG_EDX, UC_X86_REG_EFLAGS

if len(sys.argv) != 3:
    raise SystemExit("usage: original_depth_check.py GAME_DIRECTORY NATIVE_DEPTH_TEST")
rng = random.Random(0x52c4a3)
cases = [(32768,0,0,0,0,32738), (32768,0,0,0,0,32737),
         (32768,0,0,0,0,32739), (0,0,0,0,30,0)]
for level in range(-128, 128):
    for base, origin, screen in ((0,0,0), (32768,0,65535), (65535,-32768,32768), (32768,219,-715)):
        cases.append((base, origin, screen, level, rng.randrange(256), rng.randrange(65536)))
for _ in range(512):
    cases.append((rng.randrange(65536), rng.randrange(-70000,70001), rng.randrange(-70000,70001),
                  rng.randrange(-128,128), rng.randrange(256), rng.randrange(65536)))
native = subprocess.check_output([sys.argv[2]], text=True,
    input="".join(" ".join(map(str, case)) + "\n" for case in cases)).splitlines()
assert len(native) == len(cases)
for name, start, stop, output, compare, source_pointer in (
        ("game.exe",0x52c4a8,0x52c4dd,0xa52f54,0x52d2c4,0xa52f0c),
        ("gamemd.exe",0x547f68,0x547f9d,0xaa1104,0x548d84,0xaa10bc)):
    cpu = load_original(Path(sys.argv[1]) / name)
    memory, stack = 0x1000000, 0x100f000
    cpu.mem_map(memory, 0x10000)
    def word(address, value):
        cpu.mem_write(address, struct.pack("<I", value & 0xffffffff))
    for case, line in zip(cases, native):
        base, origin, screen, level, source, existing = case
        word(memory + 0x24, base)
        word(memory + 4, origin)
        word(memory + 0x10c, 30)
        word(stack + 0x40, screen)
        word(stack + 0x54, level)
        cpu.reg_write(UC_X86_REG_EAX, memory)
        cpu.reg_write(UC_X86_REG_ECX, 0)
        cpu.reg_write(UC_X86_REG_EDI, memory + 0x100)
        cpu.reg_write(UC_X86_REG_ESP, stack)
        cpu.emu_start(start, stop, count=100)
        word(source_pointer, memory + 0x200)
        cpu.mem_write(memory + 0x200, bytes([source]))
        cpu.mem_write(memory + 0x300, struct.pack("<H", existing))
        cpu.reg_write(UC_X86_REG_ECX, memory + 0x300)
        cpu.emu_start(compare, compare + 0x15, count=100)
        depth = cpu.reg_read(UC_X86_REG_EDX) & 65535
        visible = not (cpu.reg_read(UC_X86_REG_EFLAGS) & 1)
        assert tuple(map(int,line.split())) == (depth,int(visible)), (name,case,line,depth)
    print(f"{name}: {len(cases)} terrain depth cases match, including all signed levels and 16-bit wrap")
