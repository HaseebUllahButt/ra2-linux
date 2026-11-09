#!/usr/bin/env python3
"""Check 0070CD10 score arithmetic with resolved virtual and distance outputs.
Requires Unicorn. Usage: original_score_check.py /path/to/gamemd.exe
The square-root helper returns the supplied resolved distance of 1792.
"""
import pathlib
import struct
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_FPCW
image = pathlib.Path(sys.argv[1]).read_bytes()
base = '--base' in sys.argv
entry, sqrt_helper = (0x6d7550,0x4ba410) if base else (0x70cd10,0x4cac40)
cases = [(False, False, False, True, 99780.),
         (True, False, False, True, 100180.),
         (True, True, False, True, 100580.),
         (True, True, True, True, 82730.),
         (True, True, True, False, 0.)]
for targets_attacker, preferred, explicit_origin, type_present, expected in cases:
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    cpu.mem_map(0x400000, 0x500000)
    cpu.mem_write(0x400000, image[:0x500000])
    cpu.mem_map(0xa00000, 0x200000)
    cpu.mem_map(0x1000000, 0x20000)
    cpu.reg_write(UC_X86_REG_FPCW, 0xe7f)

    def word(address, value):
        if base:
            remap={0x1002000+0x2b4:0x1002000+0x228,
                   0x1000000+0x21c:0x1000000+0x1b4,
                   0x1002000+0x21c:0x1002000+0x1b4,
                   0x100c000+0x5600:0x100c000+0x54f0,
                   0x8871e0:0x839848}
            address=remap.get(address,address)
        cpu.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def double(address, value):
        if base:
            if 0x1005000+0x2c8<=address<=0x1005000+0x2e8: address-=0x30
            if address==0x100e000+0x1090: address=0x100e000+0xd50
        cpu.mem_write(address, struct.pack('<d', value))

    next_stub = 0x1010000

    def stub(table, slot, result, pop=0):
        global next_stub
        address = next_stub
        next_stub += 16
        if base:
            remap={0x84:0x80,0x88:0x84,0x2e4:0x2b8,0x3f8:0x39c}
            slot=remap.get(slot,slot)
        word(table + slot, address)
        code = b'\xb8' + struct.pack('<I', result)
        code += b'\xc2' + struct.pack('<H', pop) if pop else b'\xc3'
        cpu.mem_write(address, code)

    word(0x1000000, 0x1001000)
    word(0x1002000, 0x1003000)
    stub(0x1001000, 0x84, 0x1005000)
    stub(0x1001000, 0x2e4, 0, 4)
    stub(0x1001000, 0x3f8, 0x1008000, 4)
    stub(0x1001000, 0x48, 0x100b000, 4)
    stub(0x1003000, 0x84, 0x1004000)
    stub(0x1003000, 0x88, 0x1004000 if type_present else 0)
    stub(0x1003000, 0x2c, 6)
    stub(0x1003000, 0x2e4, 0, 4)
    stub(0x1003000, 0x3f8, 0x1008100, 4)
    stub(0x1003000, 0x48, 0x100b100, 4)
    word(0x1008000, 0x1009000)
    word(0x1008100, 0x1009100)
    word(0x1009000 + 0xac, 0x100a000)
    word(0x1009100 + 0xac, 0x100a100)
    word(0x1009000 + 0xb4, 1280)
    double(0x100a000 + 0xa0, 1.)
    double(0x100a100 + 0xa0, 1.)
    word(0x1002000 + 0x6c, 400)
    word(0x1004000 + 0xa0, 400)
    word(0x1002000 + 0x2b4, 0x1000000 if targets_attacker else 0)
    word(0x1000000 + 0x21c, 0x100c000)
    word(0x1002000 + 0x21c, 0x100d000)
    cpu.mem_write(0x100c000 + (0x143 if base else 0x1fb), b'\x01')
    word(0x100c000 + 0x5600, 1 if preferred else -1)
    word(0x100d000 + 0x30, 1)
    for offset, coefficient in zip((0x2c8, 0x2d0, 0x2d8, 0x2e0, 0x2e8), (200., -200., 200., -200., -10.)):
        double(0x1005000 + offset, coefficient)
    word(0x8871e0, 0x100e000)
    double(0x100e000 + 0x1090, 400.)
    word(0x100b200, 1 if explicit_origin else 0)
    # Return an explicit resolved distance from the unrecovered table-based square-root helper.
    double(0x100f000, 1792.)
    cpu.mem_write(sqrt_helper, b'\xdd\x05' + struct.pack('<I', 0x100f000) + b'\xc3')
    cpu.mem_write(0x101e000, b'\xdd\x1d' + struct.pack('<I', 0x100f100))
    cpu.reg_write(UC_X86_REG_ECX, 0x1000000)
    cpu.reg_write(UC_X86_REG_ESP, 0x101f000)
    cpu.mem_write(0x101f000, struct.pack('<III', 0x101e000, 0x1002000, 0x100b200))
    cpu.emu_start(entry, 0x101e006, count=10000)
    actual = struct.unpack('<d', cpu.mem_read(0x100f100, 8))[0]
    assert actual == expected, (targets_attacker, preferred, explicit_origin, type_present, actual, expected)
print('5 threat scores match original x86 arithmetic with resolved virtual and distance inputs')
