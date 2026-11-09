#!/usr/bin/env python3
"""Check Type +205 across the original base UnitType reader body.

The full 0070AB40 body runs with restored constructor state after separate
ObjectType checks. The preceding TechnoType reader and INI, formatting, and
asset services are supplied. This does not establish complete loaded-type state.
"""
import os
import runpy
import struct
import sys
from itertools import product
from unicorn import UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP

if len(sys.argv)!=5:
    raise SystemExit('usage: original_unit_reader_205_check.py game.exe unit_flags_case real_map_case game_dir')
executable,native,fixture,game=sys.argv[1:]
sys.argv=[sys.argv[0],executable,native,fixture,game]
base=runpy.run_path(os.path.join(os.path.dirname(__file__),'original_object_reader_205_check.py'))
m=base['m']
constructor=base['constructor_bytes']
active=[False]
writes=[]
calls={'parent':0,'pair':0,'format':0,'compare':0}
parent_ok=[True]

def trace_write(cpu,access,address,size,value,user):
    if active[0] and address<=m.TYPE+0x205<address+size:
        writes.append((address,size,value))

def parent_reader():
    calls['parent']+=1
    m.cpu.reg_write(UC_X86_REG_EAX,int(parent_ok[0]))

def pair_reader():
    calls['pair']+=1
    target,source=m.argument(0),m.argument(3)
    m.cpu.mem_write(target,bytes(m.cpu.mem_read(source,12)))
    m.cpu.reg_write(UC_X86_REG_EAX,target)

def format_key():
    calls['format']+=1
    target=m.argument(0); m.cpu.mem_write(target,b'Key\0')
    m.cpu.reg_write(UC_X86_REG_EAX,3)

def compare_names():
    calls['compare']+=1
    left=bytes(m.cpu.mem_read(m.argument(0),32)).split(b'\0',1)[0].lower()
    right=bytes(m.cpu.mem_read(m.argument(1),32)).split(b'\0',1)[0].lower()
    m.cpu.reg_write(UC_X86_REG_EAX,int(left!=right))

m.cpu.hook_add(UC_HOOK_MEM_WRITE,trace_write)
m.bind(0x6daf60,4,parent_reader)
m.bind(0x50fa40,16,pair_reader)
m.bind(0x46c400,12,base['integer']); m.bind(0x46a970,12,base['integer'])
m.bind(0x7820f4,0,format_key); m.bind(0x781f20,0,compare_names)
for initial,mode in product((0,1),(0,1,2)):
    m.cpu.mem_write(m.TYPE,constructor)
    m.cpu.mem_write(m.TYPE+0x205,bytes([initial]))
    m.cpu.mem_write(m.TYPE+0x24,b'AMCV\0'); m.cpu.mem_write(m.TYPE+0x1c8,b'AMCV\0')
    base['bool_mode'][0]=mode
    calls.update(parent=0,pair=0,format=0,compare=0)
    m.cpu.reg_write(UC_X86_REG_ECX,m.TYPE); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100b000))
    writes.clear(); active[0]=True
    m.cpu.emu_start(0x70ab40,0x103e000,count=1000000)
    active[0]=False
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x103e000 and m.cpu.reg_read(UC_X86_REG_EAX)&255==1
    assert not writes and m.cpu.mem_read(m.TYPE+0x205,1)==bytes([initial])
    assert calls['parent']==1 and calls['pair']==1 and calls['format']==6 and calls['compare']>=1
parent_ok[0]=False
m.cpu.mem_write(m.TYPE,constructor); m.cpu.mem_write(m.TYPE+0x205,b'\x01')
calls.update(parent=0,pair=0,format=0,compare=0)
m.cpu.reg_write(UC_X86_REG_ECX,m.TYPE); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100b000))
writes.clear(); active[0]=True
m.cpu.emu_start(0x70ab40,0x103e000,count=1000000)
active[0]=False
assert m.cpu.reg_read(UC_X86_REG_EIP)==0x103e000 and m.cpu.reg_read(UC_X86_REG_EAX)&255==0
assert not writes and m.cpu.mem_read(m.TYPE+0x205,1)==b'\x01'
assert calls=={'parent':1,'pair':0,'format':0,'compare':0}
print('base x86 UnitType reader preserves supplied Type +205 in seven checked cases')
