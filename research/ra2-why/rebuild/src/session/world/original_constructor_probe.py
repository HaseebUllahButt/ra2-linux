#!/usr/bin/env python3
"""Development probe for the base Unit constructor chain.

The original constructors run with allocated fixture records. This probe is
not a game loader. It reports the first dependency that is not yet modelled.
"""
import struct
import sys
from unicorn import UcError,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
import original_route_check as r
m=r.m;cpu=m.cpu
if not m.base: raise SystemExit('base game.exe and --base are required')
r.setup((0,),15)
cpu.mem_map(0x1400000,0x40000)
cpu.mem_map(0x1500000,0x1000000)
next_allocation=0x1500000
def allocate():
    global next_allocation
    size=m.argument();address=next_allocation
    next_allocation+=(size+15)&~15
    assert next_allocation<0x2500000
    cpu.reg_write(UC_X86_REG_EAX,address)
m.bind(0x782017,0,allocate)
house,scenario=0x1400000,0x1430000
cpu.mem_write(m.FOOT,bytes([0xa5])*0x1000);cpu.mem_write(house,bytes(0x16000))
m.word(house+0x34,0x1420000)
# House constructor gives this type-count vector and fixed registry empty slots.
m.word(house+0x5434,0x1421000);m.word(house+0x5438,512)
m.word(house+0x1a78,512)
m.word(0xa3d290,scenario)
m.word(m.TYPE+0x8c,1000);m.word(m.TYPE+0xa0,1000)
locomotor_vtable=0x1438000
m.word(m.LOCO+4,locomotor_vtable)
for index,pop in ((4,4),(8,4),(12,8)):
    stub=0x1018000+index*4
    m.word(locomotor_vtable+index,stub);m.bind(stub,pop,m.ret(1))
def create_locomotor():
    m.word(cpu.reg_read(UC_X86_REG_ECX),m.LOCO+4)
    cpu.reg_write(UC_X86_REG_EAX,0)
m.bind(0x41b650,12,create_locomotor)
type_vtable=0x1439000
m.word(m.TYPE,type_vtable);m.word(type_vtable+0x40,0x1018100)
type_index=int(sys.argv[4]) if len(sys.argv)>4 else 0
excluded=int(sys.argv[5]) if len(sys.argv)>5 else 0
cpu.mem_write(m.TYPE+0x202,bytes([excluded]))
m.bind(0x1018100,0,m.ret(type_index))
cpu.reg_write(UC_X86_REG_ECX,scenario+0x218);cpu.reg_write(UC_X86_REG_ESP,0x103f000)
seed=int(sys.argv[3]) if len(sys.argv)>3 else 42
cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,seed))
cpu.emu_start(0x638790,0x103e000,count=100000)
# A preceding original House constructor consumes this range before Unit creation.
cpu.reg_write(UC_X86_REG_ECX,scenario+0x218);cpu.reg_write(UC_X86_REG_ESP,0x103f000)
cpu.mem_write(0x103f000,struct.pack('<III',0x103e000,450,1800))
cpu.emu_start(0x6388a0,0x103e000,count=100000)
random_before=bytes(cpu.mem_read(scenario+0x218,1012))
rng_calls=[]
def trace_rng(machine,address,size,user):
    if address in (0x638840,0x6388a0):
        esp=machine.reg_read(UC_X86_REG_ESP)
        rng_calls.append((hex(address),hex(m.read(esp)),m.signed(esp+4),m.signed(esp+8)))
cpu.hook_add(UC_HOOK_CODE,trace_rng)
cpu.reg_write(UC_X86_REG_ECX,m.FOOT);cpu.reg_write(UC_X86_REG_ESP,0x103f000)
cpu.mem_write(0x103f000,struct.pack('<III',0x103e000,m.TYPE,house))
try: cpu.emu_start(0x6fa400,0x103e000,count=1000000)
except UcError:
    print('unresolved constructor dependency',hex(cpu.reg_read(UC_X86_REG_EIP)),[hex(a) for a in m.recent]);raise
assert cpu.reg_read(UC_X86_REG_EIP)==0x103e000
random_after=bytes(cpu.mem_read(scenario+0x218,1012))
print('original Unit constructor returned; rng indexes',struct.unpack_from('<II',random_before,4),struct.unpack_from('<II',random_after,4))
print('rng calls',rng_calls)
for offset in (0x30,0x58,0x6c,0x70,0x80,0x88,0x98,0xa0,0x1b4,0x2e0,0x2f8,0x32c,0x4c4,0x5a8,0x5ac,0x5b4):
    print('unit',hex(offset),hex(m.read(m.FOOT+offset)))
print('house_units',m.read(house+0x228),'limbo',cpu.mem_read(m.FOOT+0x76,1)[0],
      'map_flag',cpu.mem_read(m.FOOT+0x32d,1)[0])
expected_count=0 if excluded else 1
assert m.read(house+0x228)==expected_count
assert m.read(0x1421000+4*type_index)==expected_count and m.read(house+0x1278+4*type_index)==expected_count
low=struct.unpack('<H',cpu.mem_read(m.FOOT+0x320,2))[0]
indexes=struct.unpack_from('<II',random_after,4)
table=struct.unpack_from('<250I',random_after,12)
next_id=m.read(m.FOOT+0x30)
on_map=cpu.mem_read(m.FOOT+0x32d,1)[0]
hull=struct.unpack('<H',cpu.mem_read(m.FOOT+0x2e0,2))[0]>>8
turret=struct.unpack('<H',cpu.mem_read(m.FOOT+0x2f8,2))[0]>>8
assert m.read(m.read(m.FOOT)+0x2c)==0x70a2d0
cpu.reg_write(UC_X86_REG_ECX,m.FOOT);cpu.reg_write(UC_X86_REG_ESP,0x103f000)
cpu.mem_write(0x103f000,struct.pack('<I',0x103e000))
cpu.emu_start(0x70a2d0,0x103e000,count=10000)
assert cpu.reg_read(UC_X86_REG_EAX)==1
print('RESULT',low,next_id,on_map,m.read(m.FOOT+0x6c),hull,turret,cpu.mem_read(m.FOOT+0x5a0,1)[0],
      m.read(house+0x228),m.read(0x1421000+4*type_index),m.read(house+0x1278+4*type_index),*indexes,*table)
