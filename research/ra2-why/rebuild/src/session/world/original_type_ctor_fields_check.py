#!/usr/bin/env python3
"""Check original base UnitType constructor fields at a fixed prefix boundary.

The ObjectType, TechnoType, and UnitType constructor bodies run through
0070A68B. Grandparent construction, collections, free, and string services
are supplied. Later constructor instructions and all type loading are omitted.
"""
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP

if len(sys.argv)!=3:
    raise SystemExit('usage: original_type_ctor_fields_check.py game.exe unit_flags_case')
executable,native=sys.argv[1:]
expected=list(map(int,subprocess.check_output([native,'--constructor-vectors'],text=True).split()))
sys.argv=[sys.argv[0],executable,'--base']
import original_move_trace as m

calls={'grandparent':0,'string':0}
def grandparent():
    calls['grandparent']+=1
    m.cpu.reg_write(UC_X86_REG_EAX,m.cpu.reg_read(UC_X86_REG_ECX))
def collection():
    m.cpu.reg_write(UC_X86_REG_EAX,m.cpu.reg_read(UC_X86_REG_ECX))
def copy_string():
    calls['string']+=1
    target,source,count=m.argument(0),m.argument(1),m.argument(2)
    assert count==24
    raw=bytes(m.cpu.mem_read(source,count)).split(b'\0',1)[0][:count]
    m.cpu.mem_write(target,raw+bytes(count-len(raw)))
    m.cpu.reg_write(UC_X86_REG_EAX,target)

m.bind(0x410580,4,grandparent)
m.bind(0x781d3b,0,m.ret(0))
m.bind(0x7823d0,0,copy_string)
for address in (0x46d020,0x50b860,0x454ad0,0x4f89a0,0x654960):
    m.bind(address,8,collection)
for fill in (0,0xa5,0xff):
    for address in (0xa72f98,0xa72fa0,0xa72fa4,0xac0cb8,0xac0cc0,0xac0cc4,
                    0xa40ac0,0xa40ac8,0xa40acc,0xa35d50,0xa35d58,0xa35d5c):
        m.word(address,0)
    m.cpu.mem_write(m.TYPE,bytes([fill])*0xc00)
    m.cpu.mem_write(0x100a000,b'AMCV\0')
    m.cpu.reg_write(UC_X86_REG_ECX,m.TYPE); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100a000))
    calls.update(grandparent=0,string=0)
    m.cpu.emu_start(0x70a600,0x70a68b,count=1000000)
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x70a68b
    assert calls['grandparent']==1 and calls['string']>=1
    actual=[m.cpu.mem_read(m.TYPE+offset,1)[0] for offset in (0x205,0xaa0,0xaf3,0xbb0,0xbb1)]
    actual.extend((m.signed(m.TYPE+0x5a4),m.signed(m.TYPE+0x500)))
    assert actual==expected
print('base x86 UnitType constructor prefix matches seven fields for three initial fills')
