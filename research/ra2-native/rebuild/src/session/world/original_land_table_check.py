#!/usr/bin/env python3
"""Check base land table writes using real rules and supplied INI services.

The original 0064D610 body executes. Section presence and parsed numeric
values are supplied from MP02T2 and the original base RULES.INI. This does
not prove the AMCV SpeedType or any live Cell state.
"""
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

if len(sys.argv) != 4:
    raise SystemExit('usage: original_land_table_check.py game.exe real_map_case game_dir')
executable, fixture, game = sys.argv[1:]
lines = subprocess.check_output([fixture, game, '--land-info'], text=True).splitlines()
values = {}
for line in lines:
    section, track, wheel = line.split()
    assert track.endswith('%') and wheel.endswith('%')
    values[section] = {'Track': float(track[:-1]) / 100, 'Wheel': float(wheel[:-1]) / 100}
assert values['Clear'] == {'Track': 1.0, 'Wheel': 1.0}
assert values['Tiberium'] == {'Track': 0.7, 'Wheel': 0.5}
sys.argv = [sys.argv[0], executable, '--base']
import original_move_trace as m

def cstring(address):
    return bytes(m.cpu.mem_read(address, 64)).split(b'\0', 1)[0].decode('ascii')

def has_section():
    m.cpu.reg_write(UC_X86_REG_EAX, int(cstring(m.argument()) in values))

def number():
    section, key = cstring(m.argument(0)), cstring(m.argument(1))
    value = values.get(section, {}).get(key, 1.0)
    m.cpu.mem_write(0x100a000, struct.pack('<d', value))

m.bind(0x50c9b0, 4, has_section)
m.bind(0x50e3a0, 16, number)
# Load the supplied double into x87 ST(0), then return with the original ABI.
m.cpu.mem_write(0x50e3a0, b'\xdd\x05\x00\xa0\x00\x01\xc2\x10\x00')
m.bind(0x50f410, 12, m.ret(0))
m.cpu.reg_write(UC_X86_REG_ECX, 0x100b000)
m.cpu.reg_write(UC_X86_REG_ESP, 0x103f000)
m.cpu.mem_write(0x103f000, struct.pack('<I', 0x103e000))
m.cpu.emu_start(0x64d610, 0x103e000, count=1000000)
assert m.cpu.reg_read(UC_X86_REG_EIP)==0x103e000, 'original land reader did not return'
def chopped_float(value):
    # original_move_trace sets the x87 control word to 0xE7F (toward zero).
    bits = struct.unpack('<I', struct.pack('<f', value))[0]
    if struct.unpack('<f', struct.pack('<I', bits))[0] > value:
        bits -= 1
    return struct.pack('<I', bits)

for index, name in enumerate(('Clear', 'Road', 'Water', 'Rock', 'Wall', 'Tiberium',
                              'Beach', 'Rough', 'Ice', 'Railroad', 'Tunnel', 'Weeds')):
    for slot, key in ((1, 'Track'), (2, 'Wheel')):
        actual = bytes(m.cpu.mem_read(0x850ea0 + 4 * (index * 9 + slot), 4))
        expected = chopped_float(values[name][key])
        assert actual == expected, (name, key, actual.hex(), expected.hex())
print('base x86 land reader writes real Track and Wheel values to separate slots')
