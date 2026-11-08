#!/usr/bin/env python3
"""Check the 006F7CA0 prefix with original x86 instructions. Requires Unicorn.
Virtual calls return explicit test inputs. Execution stops before 006F7E24.
Usage: original_target_check.py /path/to/gamemd.exe
"""
import pathlib
import struct
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP

image = pathlib.Path(sys.argv[1]).read_bytes()
cases = [
    ({}, 'Continue'),
    ({'fire': 5}, 'Reject'),
    ({'fire': 5, 'threat': 0x18200}, 'Continue'),
    ({'initial': 0, 'type394': 2}, 'Reject'),
    ({'verse': float(struct.unpack('<f', struct.pack('<f', .02))[0])}, 'Reject'),
    ({'verse': float('nan')}, 'Reject'),
    ({'type604': 2, 'slot50': 1, 'cell_ec': 1}, 'Reject'),
    ({'flag81': 1}, 'Reject'),
    ({'hp': 0}, 'Reject'),
    ({'state220': 2, 'visible': 0, 'same_house': 0}, 'Reject'),
    ({'state220': 2, 'visible': 0, 'same_house': 1}, 'Continue'),
    ({'flag3d5': 0}, 'Reject'),
    ({'mission4': 1}, 'Reject'),
    ({'height': -21}, 'Reject'),
    ({'height': -20}, 'Continue'),
]
for changed, expected in cases:
    v = dict(fire=0, threat=0, initial=256, type394=0, verse=1., type604=0,
             slot50=0, cell_ec=2, flag81=0, hp=100, state220=0,
             visible=1, same_house=0, flag3d5=1, mission4=0, height=0)
    v.update(changed)
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    cpu.mem_map(0x400000, 0x500000)
    cpu.mem_write(0x400000, image[:0x500000])
    cpu.mem_map(0x1000000, 0x20000)

    def write(address, value):
        cpu.mem_write(address, struct.pack('<I', value & 0xffffffff))

    stub_address = 0x1010000

    def stub(table, slot, result, pop=0):
        global stub_address
        address = stub_address
        stub_address += 16
        write(table + slot, address)
        code = b'\xb8' + struct.pack('<I', result & 0xffffffff)
        code += b'\xc2' + struct.pack('<H', pop) if pop else b'\xc3'
        cpu.mem_write(address, code)

    write(0x1000000, 0x1001000)  # Attacker and its virtual table.
    write(0x1002000, 0x1003000)  # Target and its virtual table.
    stub(0x1001000, 0x330, 0)
    stub(0x1001000, 0x2e4, 0, 4)
    stub(0x1001000, 0x3f8, 0x1008000, 4)
    stub(0x1001000, 0x3bc, v['fire'], 8)
    stub(0x1001000, 0x84, 0x1005000)
    stub(0x1003000, 0x84, 0x1004000)
    stub(0x1003000, 0x50, v['slot50'])
    stub(0x1003000, 0x1bc, 0x1006000)
    stub(0x1003000, 0x48, 0x100b000, 8)
    stub(0x1003000, 0x1c8, v['height'])
    write(0x1008000, 0x1009000)
    write(0x1009000 + 0xac, 0x100a000)
    cpu.mem_write(0x100a000 + 0xa0, struct.pack('<d', v['verse']))
    write(0x1002000 + 0x70, v['initial'])
    write(0x1002000 + 0x6c, v['hp'])
    write(0x1005000 + 0x394, v['type394'])
    write(0x1005000 + 0x604, v['type604'])
    write(0x1006000 + 0xec, v['cell_ec'])
    cpu.mem_write(0x1002000 + 0x81, bytes([v['flag81']]))
    write(0x1002000 + 0x220, v['state220'])
    cpu.mem_write(0x1002000 + 0x3d5, bytes([v['flag3d5']]))
    cpu.mem_write(0x1007000 + 4, bytes([v['mission4']]))
    write(0x1000000 + 0x21c, 0x100c000)
    write(0x1002000 + 0x21c, 0x100c000 if v['same_house'] else 0x100d000)
    # Explicit external map/mission outputs; this check does not claim their recovery.
    cpu.mem_write(0x565730, b'\xb8' + struct.pack('<I', 0x1006000) + b'\xc2\x04\x00')
    cpu.mem_write(0x4870d0, b'\xb8' + struct.pack('<I', v['visible']) + b'\xc2\x04\x00')
    cpu.mem_write(0x5b3a00, b'\xb8' + struct.pack('<I', 0x1007000) + b'\xc3')
    result = []

    def stop_at_boundary(cpu, address, size, user):
        if address in (0x6f7e24, 0x6f894f):
            result.append('Continue' if address == 0x6f7e24 else 'Reject')
            cpu.emu_stop()

    cpu.hook_add(UC_HOOK_CODE, stop_at_boundary)
    cpu.reg_write(UC_X86_REG_ECX, 0x1000000)
    cpu.reg_write(UC_X86_REG_ESP, 0x101f000)
    cpu.mem_write(0x101f000, struct.pack('<8I', 0x101e000, v['threat'], 0, 0, 0x1002000, 0, 0, 0))
    cpu.emu_start(0x6f7ca0, 0x101e000, count=10000)
    assert result == [expected], (changed, result, expected)
print('15 target prefix decisions match original x86 instructions with resolved virtual inputs')
