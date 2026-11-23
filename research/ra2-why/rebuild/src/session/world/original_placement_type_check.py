#!/usr/bin/env python3
"""Check the base UnitType placement reader with supplied INI records.

The original constructor and UnitType reader fragment execute. Grandparent,
collection, string, runtime, and INI record services are supplied. Other type
loading and map phases remain omitted.
"""
import os
import runpy
import struct
import subprocess
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBP,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDI,UC_X86_REG_EIP,UC_X86_REG_ESP

if len(sys.argv) not in (4,5):
    raise SystemExit('usage: original_placement_type_check.py game.exe real_map_case game_dir [unit_flags_case]')
executable,fixture,game=sys.argv[1:4]
native_case=sys.argv[4] if len(sys.argv)==5 else None
installed=subprocess.check_output([fixture,game,'--amcv-placement-info'],text=True).split()
assert installed==['<absent>','<absent>','<absent>']
sys.argv=[sys.argv[0],executable,fixture,game]
base=runpy.run_path(os.path.join(os.path.dirname(__file__),'original_speed_type_check.py'))
m=base['m']
from original_image import load_original

names=('AMCV','MovementRestrictedTo','CanBeach','SmallVisceroid','LargeVisceroid')
hashes=base['original_hashes'](names)
ini,section_cache,section,entry=(base[name] for name in ('ini','section_cache','section','entry'))
original=load_original(executable)
m.cpu.mem_write(0x5d57f0,bytes(original.mem_read(0x5d57f0,3)))
m.cpu.ctl_remove_cache(0x5d57f0,0x5d57f3)
m.callbacks.pop(0x5d57f0)
m.bind(0x410580,4,m.ret(0))
m.bind(0x40ffd0,4,m.ret(0))
m.bind(0x781d3b,0,m.ret(0))
def copy_string():
    target,source,count=m.argument(0),m.argument(1),m.argument(2)
    assert count<=256
    raw=bytes(m.cpu.mem_read(source,count)).split(b'\0',1)[0][:count]
    m.cpu.mem_write(target,raw+bytes(count-len(raw)))
    m.cpu.reg_write(UC_X86_REG_EAX,target)
m.bind(0x7823d0,0,copy_string)
for address in (0x46d020,0x50b860,0x454ad0,0x4f89a0,0x654960):
    m.bind(address,8,lambda:m.cpu.reg_write(UC_X86_REG_EAX,m.cpu.reg_read(UC_X86_REG_ECX)))
calls=[]
def trace(cpu,address,size,user):
    if address in (0x46a970,0x50f410): calls.append(address)
m.cpu.hook_add(UC_HOOK_CODE,trace)

def signed32(value):
    return value if value<0x80000000 else value-0x100000000

cases=({}, {'MovementRestrictedTo':'Water'}, {'SmallVisceroid':'yes'}, {'LargeVisceroid':'yes'},
       {'MovementRestrictedTo':'<none>'}, {'SmallVisceroid':'yes','LargeVisceroid':'yes'})
results=[]
for values in cases:
    for address in (0xa72f98,0xa72fa0,0xa72fa4,0xac0cb8,0xac0cc0,0xac0cc4,
                    0xa40ac0,0xa40ac8,0xa40acc,0xa35d50,0xa35d58,0xa35d5c):
        m.word(address,0)
    m.cpu.mem_write(m.TYPE,b'\xa5'*0xc00)
    m.cpu.mem_write(0x100a000,b'AMCV\0')
    m.cpu.reg_write(UC_X86_REG_ECX,m.TYPE); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100a000))
    m.cpu.emu_start(0x70a600,0x103e000,count=1000000)
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x103e000
    assert m.signed(m.TYPE+0xb94)==-1 and m.cpu.mem_read(m.TYPE+0xbb0,2)==b'\0\0'
    records=[]
    for index,(name,text) in enumerate(values.items()):
        node=0x1019000+index*0x200; value=node+0x100
        m.cpu.mem_write(value,text.encode('ascii')+b'\0')
        m.word(node+0x10,value); records.append((hashes[name],node))
    records.sort(key=lambda pair:signed32(pair[0]))
    for index,(digest,node) in enumerate(records):
        m.word(entry+index*8,digest); m.word(entry+index*8+4,node)
    m.word(section_cache,hashes['AMCV']); m.word(section_cache+4,section)
    m.word(section+0x2c,entry); m.word(section+0x30,len(records)); m.word(section+0x38,1)
    m.word(section+0x3c,0)
    m.cpu.reg_write(UC_X86_REG_EDI,m.TYPE); m.cpu.reg_write(UC_X86_REG_EBP,0x100a000)
    m.cpu.reg_write(UC_X86_REG_EBX,ini); m.cpu.reg_write(UC_X86_REG_EAX,0)
    m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    calls.clear()
    # The preceding Passive result is supplied; CanBeach is absent.
    m.cpu.emu_start(0x70ad45,0x70adb3,count=100000)
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x70adb3
    assert calls==[0x46a970,0x50f410,0x50f410,0x50f410]
    actual=(m.signed(m.TYPE+0xb94),m.cpu.mem_read(m.TYPE+0xbb0,1)[0],m.cpu.mem_read(m.TYPE+0xbb1,1)[0])
    expected=(2 if values.get('MovementRestrictedTo')=='Water' else -1,
              int('SmallVisceroid' in values),int('LargeVisceroid' in values))
    assert actual==expected
    results.append(actual)
if native_case:
    native=[tuple(map(int,line.split())) for line in subprocess.check_output([native_case,'--placement-vectors'],text=True).splitlines()]
    assert native==results
print('base x86 AMCV restricted land and Visceroid reader cases pass')
