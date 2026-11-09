#!/usr/bin/env python3
"""Check Type +205 through the original base TechnoType reader.

ObjectType readers and the full TechnoType reader execute. INI, formatting,
color, asset, enumeration, allocation, and rules-global services are supplied.
Complete type loading remains omitted.
"""
import os
import runpy
import struct
import sys
from itertools import product
from unicorn import UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP

if len(sys.argv)!=5:
    raise SystemExit('usage: original_techno_reader_205_check.py game.exe unit_flags_case real_map_case game_dir')
executable,native,fixture,game=sys.argv[1:]
sys.argv=[sys.argv[0],executable,native,fixture,game]
base=runpy.run_path(os.path.join(os.path.dirname(__file__),'original_object_reader_205_check.py'))
m=base['m']; constructor=base['constructor_bytes']
active=[False]; writes=[]
def trace(cpu,access,address,size,value,user):
    if active[0] and address<=m.TYPE+0x205<address+size:
        writes.append((address,size,value))
m.cpu.hook_add(UC_HOOK_MEM_WRITE,trace)
m.cpu.mem_map(0x1200000,0x100000)
heap=[0x1200000]
def allocate():
    size=m.argument(0); address=heap[0]; heap[0]+=(size+15)&~15
    if heap[0]>0x1300000: raise RuntimeError('fixture heap exhausted')
    m.cpu.reg_write(UC_X86_REG_EAX,address)
m.bind(0x782630,0,allocate); m.bind(0x78266e,0,allocate)
m.word(0x839848,0x101c000); m.cpu.mem_write(0x101c000,bytes(0x2000))
m.bind(0x5d6730,0,m.ret(0)); m.bind(0x5d72b0,0,m.ret(0))
m.bind(0x46c400,12,base['integer'])
m.bind(0x46c9d0,12,base['integer']); m.bind(0x782ec2,0,m.ret(0))
# Return the supplied double default in x87 ST(0).
m.bind(0x50e3a0,16,lambda:None)
m.cpu.mem_write(0x50e3a0,b'\xdd\x44\x24\x0c\xc2\x10\x00')
for initial,mode in product((0,1),(0,1,2)):
    m.cpu.mem_write(m.TYPE,constructor)
    m.cpu.mem_write(m.TYPE+0x205,bytes([initial]))
    m.cpu.mem_write(m.TYPE+0x24,b'AMCV\0'); m.cpu.mem_write(m.TYPE+0x3d,b'\0')
    m.cpu.mem_write(m.TYPE+0x64,b'AMCV\0'); m.cpu.mem_write(m.TYPE+0x1c8,b'AMCV\0')
    m.cpu.mem_write(m.TYPE+0x1e3,b'\0'); m.cpu.mem_write(m.TYPE+0x206,b'\0')
    base['parent_result'][0]=1; base['bool_mode'][0]=mode
    base['service_calls'].update(section=0,boolean=0,integer=0,string=0,color=0,format=0,load=0)
    m.cpu.reg_write(UC_X86_REG_ECX,m.TYPE); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100b000))
    writes.clear(); active[0]=True
    m.cpu.emu_start(0x6daf60,0x103e000,count=1000000)
    active[0]=False
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x103e000 and m.cpu.reg_read(UC_X86_REG_EAX)&255==1
    assert not writes and m.cpu.mem_read(m.TYPE+0x205,1)==bytes([initial])
    assert m.cpu.mem_read(m.TYPE+0xaa0,1)==bytes([int(mode==1)])
    assert base['service_calls']['section']==1 and base['service_calls']['boolean']==101
print('base x86 TechnoType reader preserves supplied Type +205 in six checked cases')
