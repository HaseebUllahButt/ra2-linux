#!/usr/bin/env python3
"""Check the base AMCV Crusher and missing SpeedType reader fragments.

The real map has no AMCV section. The base rules provide Crusher=yes and
omit SpeedType. INI service results are supplied to the original x86 code.
The complete type-loading sequence and live Unit state are not reconstructed.
"""
import struct
import subprocess
import sys
import os
import runpy
from unicorn import UC_HOOK_MEM_READ
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP

if len(sys.argv) != 4:
    raise SystemExit('usage: original_speed_type_check.py game.exe real_map_case game_dir')
executable, fixture, game = sys.argv[1:]
assert subprocess.check_output([fixture, game, '--amcv-speed-info'], text=True).strip() == 'yes absent'
# Run the real land loader first. The same original image and table remain in
# the shared emulator for the following constructor and lookup fragments.
sys.argv = [sys.argv[0], executable, fixture, game]
runpy.run_path(os.path.join(os.path.dirname(__file__), 'original_land_table_check.py'))
import original_move_trace as m

def cstring(address):
    return bytes(m.cpu.mem_read(address, 64)).split(b'\0', 1)[0].decode('ascii')

def crusher():
    assert cstring(m.argument(0)) == 'AMCV' and cstring(m.argument(1)) == 'Crusher'
    m.cpu.reg_write(UC_X86_REG_EAX, 1) # Supplied parse of Crusher=yes.

selected = [None]
def speed_type():
    assert cstring(m.argument(0)) == 'AMCV' and cstring(m.argument(1)) == 'SpeedType'
    if selected[0] is None:
        m.cpu.reg_write(UC_X86_REG_EAX, 0)
    else:
        m.cpu.mem_write(m.argument(3), b'Wheel\0')
        m.cpu.reg_write(UC_X86_REG_EAX, 1)

def compare_names():
    left, right = cstring(m.argument(0)), cstring(m.argument(1))
    m.cpu.reg_write(UC_X86_REG_EAX, 0 if left.lower() == right.lower() else 1)

m.bind(0x50f410, 12, crusher)
m.bind(0x50e9a0, 20, speed_type)
m.bind(0x781f20, 0, compare_names)
m.cpu.mem_write(0x100a000, b'AMCV\0')
# Execute the UnitType constructor through the parent write of +5A4.
# Parent object construction and collection helpers are supplied.
m.bind(0x5d57f0, 4, m.ret(0))
for address in (0x46d020, 0x50b860, 0x454ad0, 0x4f89a0, 0x654960):
    m.bind(address, 8, lambda: m.cpu.reg_write(UC_X86_REG_EAX, m.cpu.reg_read(UC_X86_REG_ECX)))
reads=[]
def trace_read(cpu,access,address,size,value,user):
    if 0x850ea0<=address<0x851054:
        reads.append((address,size))
m.cpu.hook_add(UC_HOOK_MEM_READ, trace_read)

for override, expected_index in ((None,1),('Wheel',2)):
    selected[0]=override
    m.cpu.mem_write(m.TYPE, b'\xa5'*0xc00)
    m.cpu.reg_write(UC_X86_REG_ECX, m.TYPE)
    m.cpu.reg_write(UC_X86_REG_ESP, 0x103f000)
    m.cpu.mem_write(0x103f000, struct.pack('<II', 0x103e000, 0x100a000))
    m.cpu.emu_start(0x70a600, 0x6da05f, count=100000)
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x6da05f, 'constructor prefix did not reach the +5A4 write boundary'
    assert m.read(m.TYPE+0x5a4)==0xffffffff
    m.cpu.reg_write(UC_X86_REG_EBP, m.TYPE)
    m.cpu.reg_write(UC_X86_REG_ESI, 0x100b000)
    m.cpu.reg_write(UC_X86_REG_EBX, 0x100a000)
    m.cpu.reg_write(UC_X86_REG_ESP, 0x103f000)
    # Base 006DD104 reads Crusher and writes Type +AF3.
    m.cpu.emu_start(0x6dd104, 0x6dd11e, count=1000)
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x6dd11e, 'Crusher reader did not reach its write boundary'
    assert m.cpu.mem_read(m.TYPE+0xaf3,1)==b'\x01'
    m.cpu.reg_write(UC_X86_REG_EDI, m.TYPE)
    m.cpu.reg_write(UC_X86_REG_EBX, 0x100b000)
    m.cpu.reg_write(UC_X86_REG_EBP, 0x100a000)
    m.cpu.reg_write(UC_X86_REG_ESP, 0x103f000)
    # Base 0070ABEA chooses default 1; 0046C400 also accepts an explicit Wheel.
    m.cpu.emu_start(0x70abea, 0x70ac28, count=10000)
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x70ac28, 'SpeedType reader did not reach its write boundary'
    index=m.read(m.TYPE+0x5a4)
    assert index==expected_index
    for land, expected in ((0,0x3f800000),(5,0x3f333333 if index==1 else 0x3f000000)):
        m.word(0x100c000+0xec,land)
        m.cpu.reg_write(UC_X86_REG_EDI,0x100c000)
        m.cpu.reg_write(UC_X86_REG_ECX,index)
        reads.clear()
        # Base 00471CDD computes land*9 + SpeedType and loads the float.
        m.cpu.emu_start(0x471cdd,0x471cef,count=100)
        assert m.cpu.reg_read(UC_X86_REG_EIP)==0x471cef, 'land lookup did not reach its load boundary'
        address=0x850ea0+4*(land*9+index)
        assert reads==[(address,4)] and m.read(address)==expected
print('base x86 AMCV default and Wheel override read separate real land values')
