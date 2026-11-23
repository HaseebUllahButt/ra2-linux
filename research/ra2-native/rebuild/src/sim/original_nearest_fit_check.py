#!/usr/bin/env python3
"""Run original nearest search, rectangle, Cell fit, and projection together.

Usage: original_nearest_fit_check.py executable [--base] [--edges] [--vectors]
Usable-area, component answers, raw Cell metadata and static land data are
supplied. All four decision bodies execute unchanged. Live world binding stays open.
"""
import struct
import sys
import original_nearest_cell_check as m
from original_image import load_original
from unicorn import UC_HOOK_CODE,UC_HOOK_MEM_READ
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP
pristine=load_original(sys.argv[1])
rect=0x550cf0 if m.base else 0x56e7c0
projection=0x6a6a50 if m.base else 0x6d6410
for address in (rect,projection):m.cpu.mem_write(address,bytes(pristine.mem_read(address,3)))
del m.callbacks[rect]
old_inputs=m.inputs
def inputs(seed):
    s=old_inputs(seed)
    if seed%17==0:s['zone']=12
    return s
m.inputs=inputs
TABLE=0x850ea0 if m.base else 0x89ea40
REGISTRY,OVERLAY=0x1203000,0x1204000
m.word(0xa35dec if m.base else 0xa83d84,REGISTRY);m.word(REGISTRY,OVERLAY)
m.cpu.mem_write(OVERLAY+(0x278 if m.base else 0x2a8),b'\x01')
def speed_bits(land,speed):return (0x3f800000,0x3f333333,0x3f000000,0,0xbf800000,0x7fc00001)[(m.state['seed']+land*3+speed)%6]
old_metadata=m.write_metadata
configured=None
def metadata(address,cell):
    global configured
    usable=old_metadata(address,cell);h=m.hashed(cell);seed=m.state['seed'];profile=m.state['profile']
    flags=m.read(address+0x140)|(0x1000 if h&0x1000 else 0);m.word(address+0x140,flags)
    m.cpu.mem_write(address+0x124,bytes([32 if profile==4 or (h>>4)%8==0 else 0]))
    m.cpu.mem_write(address+0x128,bytes([32 if profile==4 or (h>>8)%8==0 else 0]))
    m.word(address+0x44,0 if profile in (2,5) and h%3==0 else -1);m.word(address+0xec,(h>>12)%12)
    if configured!=seed:
        m.cpu.mem_write(TABLE,struct.pack('<108I',*(speed_bits(land,speed) for land in range(12) for speed in range(9))))
        m.cpu.mem_write(OVERLAY+(0x1fd if m.base else 0x22d),bytes([seed&1]));configured=seed
    return usable
m.write_metadata=metadata
def rect_observer(cpu,address,size,user):
    esp=cpu.reg_read(UC_X86_REG_ESP);raw=m.read(esp+0x2c);obj=m.read(cpu.reg_read(UC_X86_REG_ECX)+0x24)
    m.events.append((6,raw,obj,0))
observer=0x550d62 if m.base else 0x56e832
m.cpu.hook_add(UC_HOOK_CODE,rect_observer,begin=observer,end=observer)
def component():
    p=m.read(m.argument());zone=m.argument(1);above=m.argument(2)
    m.events.append((7,p,zone,above));expected=0xffffffff if m.state['component']==0xffff else m.state['component']
    m.cpu.reg_write(UC_X86_REG_EAX,expected)
m.bind(0x54f8f0 if m.base else 0x56d230,12,component)
def table_read(cpu,access,address,size,value,user):
    assert size==4 and (address-TABLE)%4==0
    index=(address-TABLE)//4;m.events.append((8,index//9,index%9,m.read(address)))
m.cpu.hook_add(UC_HOOK_MEM_READ,table_read,begin=TABLE,end=TABLE+431)
projection_out=0
def projection_enter():
    global projection_out
    projection_out=m.argument();x,y,z=struct.unpack('<iii',m.cpu.mem_read(m.argument(1),12));assert z==0
    m.events.append((3,x&0xffffffff,y&0xffffffff,z))
m.callbacks[projection]=projection_enter
def projection_return():
    assert m.cpu.reg_read(UC_X86_REG_EAX)==projection_out;m.events.append((5,m.read(projection_out),0,0))
for address in (0x6a6bb8,0x6a6bc8) if m.base else (0x6d6578,0x6d6588):
    assert bytes(m.cpu.mem_read(address,3))==b'\xc2\x08\x00'
    m.callbacks[address]=projection_return;m.cpu.hook_add(UC_HOOK_CODE,m.hook,begin=address,end=address)
def lookup():
    cell=m.read(m.argument());address=m.GOAL if cell==m.state['goal'] else m.FALLBACK
    m.word(address+0x24,cell);metadata(address,cell)
    level=m.cpu.mem_read(address+0x11b,1)[0];level=level if level<128 else level-256
    m.events.append((4,cell,level&0xffffffff,m.read(address+0x140)));m.cpu.reg_write(UC_X86_REG_EAX,address)
m.bind(0x548070 if m.base else 0x5657a0,4,lookup)
total=2166136261
for seed in range(256):
    h=m.run(seed)
    if '--vectors' in sys.argv:print(seed,f'{h:08x}')
    total=((total^h)*16777619)&0xffffffff
expected=(0x1c425af5 if '--edges' in sys.argv else 0x9debbbcd) if m.base else (0x671a4eec if '--edges' in sys.argv else 0x0f92a915)
assert total==expected,'original composed nearest and Cell fit vectors changed'
print('256 original nearest searches with rectangle, Cell fit and projection',f'{total:08x}',file=sys.stderr)
