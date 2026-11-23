#!/usr/bin/env python3
"""Run original cell projection with resolved, read-only map lookup answers.

Usage: original_cell_projection_check.py executable [--base] [--vectors]
Only Map::GetCell is adapted. Coordinate conversion, diagonal steps, signed
heights, raw flag branches and returned Cell execute unchanged in both editions.
"""
import struct
import sys
from original_image import load_original
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP
cpu=load_original(sys.argv[1]);base='--base' in sys.argv
cpu.mem_map(0x1000000,0x100000)
CELL,OUT,POINT,STACK,STOP=0x1000000,0x1001000,0x1002000,0x10ff000,0x10fe000
lookup_address=0x548070 if base else 0x5657a0
state={};events=[]
def word(address,value):cpu.mem_write(address,struct.pack('<I',value&0xffffffff))
def read(address):return struct.unpack('<I',cpu.mem_read(address,4))[0]
def pack(x,y):return (x&0xffff)|((y&0xffff)<<16)
def unpack(value):return struct.unpack('<hh',struct.pack('<I',value))
def trunc(value):return value//256 if value>=0 else -((-value)//256)
def signed(value):return value if value<0x80000000 else value-0x100000000
def inputs(seed):
    value=seed^0x6d1c7a32
    def next_value():
        nonlocal value
        value=(value*1664525+1013904223)&0xffffffff;return value>>16
    x=(next_value()%516-2)*256+next_value()%511-255
    y=(next_value()%516-2)*256+next_value()%511-255
    z=signed((next_value()<<16)|next_value())
    edge=(-2147483648,2147483647,-257,-256,-255,-129,-128,-1,0,1,127,128,255,256,257,8388352)
    if seed%3==0:x=edge[seed%16]
    if seed%5==0:y=edge[(seed//5)%16]
    return dict(seed=seed,profile=seed%8,point=(x,y,z),source=pack(trunc(x),trunc(y)))
def metadata(cell):
    x,y=unpack(cell);seed=state['seed'];profile=state['profile']
    h=(x*0x045d9f3b+y*0x119de1f3+seed*0x9e3779b9)&0xffffffff;level=0
    if cell==state['source']:level=127 if profile==2 else -128 if profile==3 else 0 if profile in (0,4,5) else seed%7-3
    elif profile==1:level=seed%7-3+(h>>3)%11-5
    elif profile==2:level=-128
    elif profile==3:level=127
    elif profile==6:level=h%31-15
    elif profile==7:level=(h&255)-128
    flag=False if profile==0 else True if profile in (4,5) else seed%2==0
    bridge=False if profile in (0,5) else True if profile==4 else bool(h&0x100)
    flags=(seed*2654435761&0xffffffff)&~0x1100
    flags|=0x1000 if flag else 0;flags|=0x100 if bridge else 0
    return level,flags
def lookup(cpu,address,size,user):
    cell=read(read(cpu.reg_read(UC_X86_REG_ESP)+4))
    level,flags=metadata(cell);events.append((cell,level&0xffffffff,flags,0))
    word(CELL+0x24,cell);cpu.mem_write(CELL+0x11b,bytes([level&255]));word(CELL+0x140,flags)
    cpu.reg_write(UC_X86_REG_EAX,CELL)
cpu.hook_add(UC_HOOK_CODE,lookup,begin=lookup_address,end=lookup_address)
cpu.mem_write(lookup_address,b'\xc2\x04\x00')
def rng(cpu,address,size,user):raise AssertionError('cell projection consumed shared RNG')
for address in (0x638840,0x6388a0) if base else (0x65c780,0x65c7e0):
    cpu.hook_add(UC_HOOK_CODE,rng,begin=address,end=address)
def run(seed):
    global state,events
    state=inputs(seed);events=[]
    point=struct.pack('<iii',*state['point']);cpu.mem_write(POINT,point)
    cpu.mem_write(STACK,struct.pack('<III',STOP,OUT,POINT));cpu.reg_write(UC_X86_REG_ESP,STACK)
    cpu.emu_start(0x6a6a50 if base else 0x6d6410,STOP,count=1000000)
    assert cpu.reg_read(UC_X86_REG_EIP)==STOP and cpu.reg_read(UC_X86_REG_EAX)==OUT
    assert bytes(cpu.mem_read(POINT,12))==point,'original projection changed input coordinates'
    assert 3<=len(events)<=210,'original projection step count outside proved bound'
    digest=2166136261
    for value in (read(OUT),len(events)-2,len(events),*(word for event in events for word in event)):
        digest=((digest^value)*16777619)&0xffffffff
    return digest
total=2166136261
for seed in range(4096):
    digest=run(seed)
    if '--vectors' in sys.argv:print(seed,f'{digest:08x}')
    total=((total^digest)*16777619)&0xffffffff
assert total==0x8e108462, 'original cell projection vectors changed'
print('4096 original cell projections',f'{total:08x}',file=sys.stderr)
