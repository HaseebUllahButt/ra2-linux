#!/usr/bin/env python3
"""Check original base UnitType constructor fields at constructor return.

The ObjectType, TechnoType, and UnitType constructor bodies run to return.
Grandparent construction, collections, registration capacity, free, string,
and runtime callback services are supplied. Type loading is omitted.
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

calls={'grandparent':0,'string':0,'runtime':0}
def grandparent():
    calls['grandparent']+=1
    m.cpu.reg_write(UC_X86_REG_EAX,m.cpu.reg_read(UC_X86_REG_ECX))
def collection():
    m.cpu.reg_write(UC_X86_REG_EAX,m.cpu.reg_read(UC_X86_REG_ECX))
def runtime_callback():
    calls['runtime']+=1
    m.cpu.reg_write(UC_X86_REG_EAX,0)
def copy_string():
    calls['string']+=1
    target,source,count=m.argument(0),m.argument(1),m.argument(2)
    assert count==24
    raw=bytes(m.cpu.mem_read(source,count)).split(b'\0',1)[0][:count]
    m.cpu.mem_write(target,raw+bytes(count-len(raw)))
    m.cpu.reg_write(UC_X86_REG_EAX,target)

m.bind(0x410580,4,grandparent)
m.bind(0x40ffd0,4,runtime_callback)
m.bind(0x781d3b,0,m.ret(0))
m.bind(0x7823d0,0,copy_string)
for address in (0x46d020,0x50b860,0x454ad0,0x4f89a0,0x654960):
    m.bind(address,8,collection)
for fill,registry_capacity in ((0,0),(0,1),(0xa5,0),(0xa5,1),(0xff,0),(0xff,1)):
    for address in (0xa72f98,0xa72fa0,0xa72fa4,0xac0cb8,0xac0cc0,0xac0cc4,
                    0xa40ac0,0xa40ac8,0xa40acc,0xa35d50,0xa35d58,0xa35d5c):
        m.word(address,0)
    m.word(0xa35d50,registry_capacity)
    m.word(0xa35d4c,0x1009000)
    m.cpu.mem_write(m.TYPE,bytes([fill])*0xc00)
    m.cpu.mem_write(0x100a000,b'AMCV\0')
    m.cpu.reg_write(UC_X86_REG_ECX,m.TYPE); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100a000))
    calls.update(grandparent=0,string=0,runtime=0)
    m.cpu.emu_start(0x70a600,0x103e000,count=1000000)
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x103e000
    assert m.cpu.reg_read(UC_X86_REG_EAX)==m.TYPE and m.cpu.reg_read(UC_X86_REG_ESP)==0x103f008
    assert calls['grandparent']==1 and calls['string']>=1 and calls['runtime']==1
    assert m.signed(m.TYPE+0xb90)==(0 if registry_capacity else -1)
    assert m.read(0xa35d58)==registry_capacity
    if registry_capacity: assert m.read(0x1009000)==m.TYPE
    actual=[m.cpu.mem_read(m.TYPE+offset,1)[0] for offset in (0x205,0xaa0,0xaf3,0xbb0,0xbb1)]
    actual.extend((m.signed(m.TYPE+0x5a4),m.signed(m.TYPE+0x500),m.signed(m.TYPE+0xb94)))
    assert actual==expected
print('base x86 UnitType constructor matches eight fields across fills and registry cases')
