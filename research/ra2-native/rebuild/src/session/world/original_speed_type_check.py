#!/usr/bin/env python3
"""Check the base AMCV Crusher and missing SpeedType reader fragments.

The real map has no AMCV section. The base rules provide Crusher=yes and
omit SpeedType. The original hash, cached-entry search, boolean, and string
readers run with supplied INI records. Other type-loading phases remain omitted.
"""
import struct
import subprocess
import sys
import os
import runpy
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP

if len(sys.argv) not in (4,5):
    raise SystemExit('usage: original_speed_type_check.py game.exe real_map_case game_dir [speed_type_case]')
executable, fixture, game = sys.argv[1:4]
native_case=sys.argv[4] if len(sys.argv)==5 else None
rules_tokens=subprocess.check_output([fixture, game, '--amcv-speed-info'], text=True).split()
assert rules_tokens==['yes','absent']
real_crusher=rules_tokens[0].encode('ascii')
# Run the real land loader first. The same original image and table remain in
# the shared emulator for the following constructor and lookup fragments.
sys.argv = [sys.argv[0], executable, fixture, game]
runpy.run_path(os.path.join(os.path.dirname(__file__), 'original_land_table_check.py'))
import original_move_trace as m
from original_image import load_original

def cstring(address):
    return bytes(m.cpu.mem_read(address, 64)).split(b'\0', 1)[0].decode('ascii')

def original_hashes(names=('AMCV','Crusher','SpeedType')):
    cpu=load_original(executable)
    cpu.mem_map(0x1000000,0x10000)
    result={}
    for name in names:
        cpu.mem_write(0x1001000,bytes(16))
        cpu.mem_write(0x1002000,name.encode('ascii')+b'\0')
        cpu.mem_write(0x100e000,struct.pack('<III',0x100f000,0x1002000,len(name)))
        cpu.reg_write(UC_X86_REG_ECX,0x1001000)
        cpu.reg_write(UC_X86_REG_ESP,0x100e000)
        cpu.emu_start(0x493fc0,0x100f000,count=10000)
        assert cpu.reg_read(UC_X86_REG_EIP)==0x100f000, 'original INI hash did not return'
        result[name]=cpu.reg_read(UC_X86_REG_EAX)
    return result

key_hashes=original_hashes()
hashes=[]

selected = [None]

def compare_names():
    left, right = cstring(m.argument(0)), cstring(m.argument(1))
    m.cpu.reg_write(UC_X86_REG_EAX, 0 if left.lower() == right.lower() else 1)

# The land test supplied a boolean service. Restore the original parser body.
original=load_original(executable)
m.cpu.mem_write(0x50f410, bytes(original.mem_read(0x50f410, 3)))
m.cpu.ctl_remove_cache(0x50f410,0x50f413)
m.callbacks.pop(0x50f410)
m.bind(0x781f20, 0, compare_names)
m.cpu.mem_write(0x100a000, b'AMCV\0')
# Supply the original INI object's cached section and Crusher entry. The
# original parser reads the actual value bytes and interprets the first byte.
ini, section_cache, section, entry, node, value = 0x100b000,0x100d000,0x100d100,0x100d200,0x100d300,0x100d400
m.word(ini+0x24,1); m.word(ini+0x30,section_cache)
m.word(section_cache,key_hashes['AMCV']); m.word(section_cache+4,section)
m.word(section+0x2c,entry); m.word(section+0x30,1); m.word(section+0x38,1); m.word(section+0x3c,entry)
m.word(entry,key_hashes['Crusher']); m.word(entry+4,node); m.word(node+0x10,value)
m.cpu.mem_write(value,b'yes\0')
m.word(0x100d610,0x100d700); m.cpu.mem_write(0x100d700,b'Wheel\0')
# Execute the UnitType constructor through the parent write of +5A4.
# Parent object construction and collection helpers are supplied.
m.bind(0x5d57f0, 4, m.ret(0))
for address in (0x46d020, 0x50b860, 0x454ad0, 0x4f89a0, 0x654960):
    m.bind(address, 8, lambda: m.cpu.reg_write(UC_X86_REG_EAX, m.cpu.reg_read(UC_X86_REG_ECX)))
reads=[]
parser_calls=[]
entry_calls=[]
def trace_parser(cpu,address,size,user):
    if address in (0x50f410,0x50e9a0):
        parser_calls.append(address)
    if address==0x493fc0:
        hashes.append(cstring(m.argument(0)))
    if address in (0x510dd0,0x511150):
        entry_calls.append(address)
m.cpu.hook_add(UC_HOOK_CODE,trace_parser)
def trace_read(cpu,access,address,size,value,user):
    if 0x850ea0<=address<0x851054:
        reads.append((address,size))
m.cpu.hook_add(UC_HOOK_MEM_READ, trace_read)

original_indexes=[]
for override, expected_index, crusher_value in ((None,1,real_crusher),('Wheel',2,real_crusher),(None,2,b'no')):
    selected[0]=override
    hashes.clear()
    parser_calls.clear()
    entry_calls.clear()
    m.cpu.mem_write(value,crusher_value+b'\0')
    if override:
        # Hashes are sorted as signed 32-bit values by the original search.
        m.word(entry,key_hashes['SpeedType']); m.word(entry+4,0x100d600)
        m.word(entry+8,key_hashes['Crusher']); m.word(entry+12,node)
        m.word(section+0x30,2); m.word(section+0x3c,entry+8)
    else:
        m.word(entry,key_hashes['Crusher']); m.word(entry+4,node)
        m.word(section+0x30,1); m.word(section+0x3c,entry)
    m.cpu.mem_write(m.TYPE, b'\xa5'*0xc00)
    m.cpu.reg_write(UC_X86_REG_ECX, m.TYPE)
    m.cpu.reg_write(UC_X86_REG_ESP, 0x103f000)
    m.cpu.mem_write(0x103f000, struct.pack('<II', 0x103e000, 0x100a000))
    m.cpu.emu_start(0x70a600, 0x6da05f, count=100000)
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x6da05f, 'constructor prefix did not reach the +5A4 write boundary'
    assert m.read(m.TYPE+0x5a4)==0xffffffff
    assert m.read(m.TYPE+0x500)==0
    m.cpu.reg_write(UC_X86_REG_EBP, m.TYPE)
    m.cpu.reg_write(UC_X86_REG_ESI, 0x100b000)
    m.cpu.reg_write(UC_X86_REG_EBX, 0x100a000)
    m.cpu.reg_write(UC_X86_REG_ESP, 0x103f000)
    # Base 006DD104 reads Crusher and writes Type +AF3.
    m.cpu.emu_start(0x6dd104, 0x6dd11e, count=1000)
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x6dd11e, 'Crusher reader did not reach its write boundary'
    assert hashes==['AMCV','Crusher']
    assert m.cpu.mem_read(m.TYPE+0xaf3,1)==(b'\x01' if crusher_value==b'yes' else b'\x00')
    m.cpu.reg_write(UC_X86_REG_EDI, m.TYPE)
    m.cpu.reg_write(UC_X86_REG_EBX, 0x100b000)
    m.cpu.reg_write(UC_X86_REG_EBP, 0x100a000)
    m.cpu.reg_write(UC_X86_REG_ESP, 0x103f000)
    # Base 0070ABEA chooses default 1; 0046C400 also accepts an explicit Wheel.
    m.cpu.emu_start(0x70abea, 0x70ac28, count=10000)
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x70ac28, 'SpeedType reader did not reach its write boundary'
    assert hashes==['AMCV','Crusher','AMCV','SpeedType']
    assert parser_calls==[0x50f410,0x50e9a0]
    expected_calls=[0x510dd0,0x511150]
    if override: expected_calls.append(0x510dd0)
    assert entry_calls==expected_calls
    index=m.read(m.TYPE+0x5a4)
    assert index==expected_index
    original_indexes.append(index)
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
if native_case:
    native_indexes=list(map(int,subprocess.check_output([native_case,'--vectors'],text=True).split()))
    assert native_indexes==original_indexes
print('base x86 AMCV entry search and value readers match the real default and Wheel override')
