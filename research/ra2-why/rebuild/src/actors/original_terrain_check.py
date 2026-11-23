#!/usr/bin/env python3
"""Check the terrain placement clamp in both owned executables with Unicorn.

The floor query is a fixed callback input. This check verifies the caller's
coordinate and height logic. The floor itself has a separate original oracle.
"""
import pathlib
import struct
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "sim"))
from original_image import load_original
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/path/to/user/ra2/game")
for filename, entry, floor_entry in (("game.exe", 0x6e5850, 0x55a280),
                                     ("gamemd.exe", 0x71e0d0, 0x578080)):
    cpu = load_original(root / filename)
    base, stack, stop = 0x1000000, 0x101f000, 0x101e000
    cpu.mem_map(base, 0x20000)
    expected_floor = 0

    def floor_callback(machine, address, size, _):
        if address != floor_entry:
            return
        sp = machine.reg_read(UC_X86_REG_ESP)
        ret = struct.unpack("<I", machine.mem_read(sp, 4))[0]
        machine.reg_write(UC_X86_REG_EAX, expected_floor & 0xffffffff)
        machine.reg_write(UC_X86_REG_ESP, sp + 8)  # THISCALL, one coordinate pointer.
        machine.reg_write(UC_X86_REG_EIP, ret)

    cpu.hook_add(UC_HOOK_CODE, floor_callback, begin=floor_entry, end=floor_entry)
    count = 0
    for x, y in ((128, 128), (640, 896), (-128, -384), (16777088, 128)):
        for incoming_z in (-104, 0, 104, 416):
            for expected_floor in (-207, 0, 52, 104, 416):
                cpu.mem_write(base, struct.pack("<iii", x, y, incoming_z))
                cpu.mem_write(stack, struct.pack("<III", stop, base + 0x100, base))
                cpu.reg_write(UC_X86_REG_ECX, base + 0x200)
                cpu.reg_write(UC_X86_REG_ESP, stack)
                cpu.emu_start(entry, stop, count=1000)
                actual = struct.unpack("<iii", cpu.mem_read(base + 0x100, 12))
                assert actual == (x, y, max(incoming_z, expected_floor)), actual
                count += 1
    print(filename, count, "original terrain coordinate and height clamp cases passed")
