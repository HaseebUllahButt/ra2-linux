#!/usr/bin/env python3
"""Check the base UnitType MovementZone reader and raw +AF5 flag.

The executable's constructor prefix, INI hash and entry searches, string
reader, and MovementZone reader run. INI records and parent helpers are
supplied. Full type loading and live Unit state are not reconstructed.
"""
import os
import runpy
import struct
import subprocess
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

if len(sys.argv) not in (4,5):
    raise SystemExit('usage: original_movement_zone_check.py game.exe real_map_case game_dir [movement_zone_case]')
executable,fixture,game=sys.argv[1:4]
native_case=sys.argv[4] if len(sys.argv)==5 else None
installed=subprocess.check_output([fixture,game,'--amcv-zone-info'],text=True).strip()
assert installed=='Normal'
sys.argv=[sys.argv[0],executable,fixture,game]
base=runpy.run_path(os.path.join(os.path.dirname(__file__),'original_speed_type_check.py'))
m=base['m']
hashes=base['original_hashes'](('AMCV','Crusher','MovementZone'))
ini,section_cache,section,entry,node,value=(base[name] for name in
    ('ini','section_cache','section','entry','node','value'))
zone_node,zone_value=0x100d600,0x100d700
called=[]
def trace(cpu,address,size,user):
    if address in (0x46a300,0x50e9a0,0x511150): called.append(address)
m.cpu.hook_add(UC_HOOK_CODE,trace)

def signed32(value):
    return value if value<0x80000000 else value-0x100000000

results=[]
for key,expected,flag in ((None,0,0),(installed,0,0),('Subterannean',6,1)):
    # The original searches a sorted key array. The table and cached section
    # are supplied, but the hash and search bodies execute.
    records=[(hashes['Crusher'],node)]
    if key is not None:
        records.append((hashes['MovementZone'],zone_node))
        m.cpu.mem_write(zone_value,key.encode('ascii')+b'\0')
        m.word(zone_node+0x10,zone_value)
    records.sort(key=lambda item:signed32(item[0]))
    for i,(digest,pointer) in enumerate(records):
        m.word(entry+i*8,digest); m.word(entry+i*8+4,pointer)
    m.word(section_cache,hashes['AMCV']); m.word(section_cache+4,section)
    m.word(section+0x2c,entry); m.word(section+0x30,len(records)); m.word(section+0x38,1)
    m.word(section+0x3c,entry+8*next(i for i,item in enumerate(records) if item[0]==hashes['Crusher']))
    m.word(node+0x10,value)
    m.cpu.mem_write(m.TYPE,b'\xa5'*0xc00)
    m.cpu.reg_write(UC_X86_REG_ECX,m.TYPE)
    m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100a000))
    m.cpu.emu_start(0x70a600,0x6da05f,count=100000)
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x6da05f and m.read(m.TYPE+0x500)==0
    m.cpu.reg_write(UC_X86_REG_EBP,m.TYPE)
    m.cpu.reg_write(UC_X86_REG_EBX,0x100a000)
    m.cpu.reg_write(UC_X86_REG_ESP,0x103e000)
    # The fragment pushes the default before reading [ESP+390].
    m.word(0x103e000+0x38c,ini)
    called.clear()
    m.cpu.emu_start(0x6de001,0x6de02c,count=100000)
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x6de02c
    assert m.read(m.TYPE+0x500)==expected and m.cpu.mem_read(m.TYPE+0xaf5,1)==bytes([flag])
    results.append((m.read(m.TYPE+0x500),m.cpu.mem_read(m.TYPE+0xaf5,1)[0]))
    assert 0x46a300 in called and 0x50e9a0 in called and 0x511150 in called
if native_case:
    native=[tuple(map(int,line.split())) for line in subprocess.check_output([native_case,'--vectors'],text=True).splitlines()]
    assert native==results
print('base x86 AMCV MovementZone present, absent, and raw +AF5 cases pass')
