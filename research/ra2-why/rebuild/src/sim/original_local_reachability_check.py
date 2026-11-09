#!/usr/bin/env python3
"""Run original local reachability, flood stack, final scan and output writes.

Usage: original_local_reachability_check.py executable [--base] [--vectors]
Fixtures supply map indexing, neighbors, usable-area, actor entry, and allocation.
Original traversal, compatibility lookup, vectors and scan instructions execute.
"""
import struct
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP
from original_image import load_original
cpu=load_original(sys.argv[1]); base='--base' in sys.argv
cpu.mem_map(0x1000000,0x40000)
CELLS,GRID,NODES,LINKS=0x1000000,0x1020000,0x1022000,0x1023000
ACTOR,VT,TYPE,OUTPUT,DATA,POOL=0x1025000,0x1026000,0x1027000,0x1028000,0x1029000,0x1030000
STACK,STOP=0x103f000,0x103e000
MAP=0x8324e0 if base else 0x87f7e8
VISITED=0xa6fbf0 if base else 0xabde48
callbacks={}; events=[]; state={}; allocation=POOL
def word(address,value): cpu.mem_write(address,struct.pack('<I',value&0xffffffff))
def read(address): return struct.unpack('<I',cpu.mem_read(address,4))[0]
def argument(n=0): return read(cpu.reg_read(UC_X86_REG_ESP)+4+4*n)
def bind(address,pop,callback):
    callbacks[address]=callback
    cpu.mem_write(address,b'\xc2'+struct.pack('<H',pop) if pop else b'\xc3')
def hook(cpu,address,size,user):
    if address in callbacks: callbacks[address]()
cpu.hook_add(UC_HOOK_CODE,hook)
def signed(value): return value if value<0x80000000 else value-0x100000000
def point(address): return struct.unpack('<hh',cpu.mem_read(address,4))
def index(point): return min(255,max(0,point[1]*16+point[0]))
def map_index():
    x,y=point(argument()); cpu.reg_write(UC_X86_REG_EAX,(y*16+x)&0xffffffff)
def neighbor():
    cell=(cpu.reg_read(UC_X86_REG_ECX)-CELLS)//0x200
    dx,dy=((0,-1),(1,-1),(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1))[argument()]
    x=min(15,max(0,cell%16+dx)); y=min(15,max(0,cell//16+dy))
    cpu.reg_write(UC_X86_REG_EAX,CELLS+(y*16+x)*0x200)
def entry():
    cell=(argument()-CELLS)//0x200; direction=argument(1); height=signed(argument(2))
    assert argument(3)==0 and argument(4)==1
    events.append((0,cell,direction,height))
    cpu.reg_write(UC_X86_REG_EAX,state['cells'][cell][3])
def usable():
    x,y=point(argument()); assert argument(1)==1
    events.append((1,x&65535,y&65535,0))
    cpu.reg_write(UC_X86_REG_EAX,int(state['cells'][index((x,y))][4]))
def allocate():
    global allocation
    result=allocation; allocation+=(argument()+15)&~15
    assert allocation<0x103d000
    cpu.reg_write(UC_X86_REG_EAX,result)
bind(0x54faf0 if base else 0x56d430,4,map_index)
bind(0x476ac0 if base else 0x481810,4,neighbor)
bind(0x55a660 if base else 0x578460,8,usable)
bind(0x782017 if base else 0x7c8e17,0,allocate)
bind(0x781d3b if base else 0x7c8b3d,0,lambda:None)
bind(0x102f000,0,lambda:cpu.reg_write(UC_X86_REG_EAX,TYPE))
bind(0x102f010,20,entry)
word(VT+(0x80 if base else 0x84),0x102f000)
word(VT+(0x194 if base else 0x1ac),0x102f010)
word(ACTOR,VT)

def inputs(seed):
    value=seed^0xd39a6721
    def next_value():
        nonlocal value
        value=(value*1664525+1013904223)&0xffffffff
        return value>>16
    level=next_value()%3; zone=next_value()%12
    if seed>=4096: zone=12
    x=next_value()%8+4; y=next_value()%8+4; cells=[]; links=[]
    for _ in range(256):
        region=next_value()%7; height=next_value()%32-16
        category=next_value()%8; entry=7 if next_value()%3 else 0; usable=next_value()%4!=0
        if seed%8==0: region=1; entry=0; usable=True
        if seed%8==1: region=1; category=0; usable=True
        if seed%8==2: entry=0
        cells.append((region,height,category,entry,usable))
    for _ in range(7): links.append([next_value()%7 for _ in range(next_value()%12)])
    initial=[(seed>>i)&7 for i in range(seed%4)]
    return dict(level=level,zone=zone,source=y*16+x,cells=cells,links=links,initial=initial)

def run(seed):
    global state,events,allocation
    state=inputs(seed); events=[]; allocation=POOL
    level=state['level']; size=1<<(level+1)
    for i,(region,height,category,_,_) in enumerate(state['cells']):
        cell=CELLS+i*0x200
        word(cell+0x24,(i%16)|((i//16)<<16)); word(cell+0x4c,category)
        cpu.mem_write(cell+0x11b,bytes([height&255]))
        for layer in range(3): cpu.mem_write(GRID+i*10+layer*2,struct.pack('<H',region))
    for i,links in enumerate(state['links']):
        word(NODES+i*0x24+4,LINKS+i*0x100); word(NODES+i*0x24+0x10,len(links))
        for j,target in enumerate(links): word(LINKS+i*0x100+j*8,target)
    word(MAP+(0x68 if base else 0x6c),256); word(MAP+(0x6c if base else 0x70),GRID)
    word(MAP+(0xf0 if base else 0xf4),7); word(MAP+(0xf4 if base else 0xf8),8)
    word(MAP+(0x8c if base else 0x90)+level*24,NODES)
    word(TYPE+(0x500 if base else 0x5b4),state['zone'])
    word(OUTPUT+4,DATA); word(OUTPUT+8,128); word(OUTPUT+0x10,len(state['initial']))
    for i,value in enumerate(state['initial']): cpu.mem_write(DATA+i*2,struct.pack('<H',value))
    cpu.mem_write(STACK,struct.pack('<5I',STOP,CELLS+state['source']*0x200,level,OUTPUT,ACTOR))
    cpu.reg_write(UC_X86_REG_ECX,MAP); cpu.reg_write(UC_X86_REG_ESP,STACK)
    cpu.emu_start(0x565eb0 if base else 0x5840c0,STOP,count=1000000)
    assert cpu.reg_read(UC_X86_REG_EIP)==STOP,'original local traversal bound exceeded'
    h=2166136261
    def mix(v):
        nonlocal h
        h=((h^(v&0xffffffff))*16777619)&0xffffffff
    mix(0); mix(cpu.reg_read(UC_X86_REG_EAX)&255)
    count=read(OUTPUT+0x10); mix(count)
    assert count<=128
    for i in range(count): mix(struct.unpack('<H',cpu.mem_read(DATA+i*2,2))[0])
    for x in range(size):
        for y in range(size): mix(cpu.mem_read(VISITED+x*8+y,1)[0])
    for event in events:
        for value in event: mix(value)
    return h

total=2166136261
assert not (base and "--zone12" in sys.argv),"base rules reject movement zone 12"
seeds=range(4096,4224) if "--zone12" in sys.argv else range(4096)
for seed in seeds:
    digest=run(seed)
    if '--vectors' in sys.argv: print(seed,f'{digest:08x}')
    total=((total^digest)*16777619)&0xffffffff
print(len(seeds),'original local reachability cases',f'{total:08x}',file=sys.stderr)
