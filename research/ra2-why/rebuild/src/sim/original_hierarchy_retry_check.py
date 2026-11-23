#!/usr/bin/env python3
"""Check original hierarchy retry control and real edge selection bodies.

Usage: original_hierarchy_retry_check.py executable [--base] [--vectors]
Region index, Cell lookup and local reachability are explicit adapters.
Original vector construction, retry loops, list writes and edge selection run.
"""
import struct
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP
import original_retry_edges_check as r
cpu=r.cpu; base=r.base
cpu.mem_map(0x1100000,0x10000)
GRID,ROOT,REPORTS,NODES,LINKS,EDGES=0x1100000,0x1101000,0x1102000,0x1103000,0x1106000,0x110a000
callbacks={}; events=[]; state={}
def argument(n=0): return r.read(cpu.reg_read(UC_X86_REG_ESP)+4+4*n)
def bind(address,pop,callback):
    callbacks[address]=callback
    cpu.mem_write(address,b'\xc2'+struct.pack('<H',pop))
def hook(cpu,address,size,user):
    if address in callbacks: callbacks[address]()
cpu.hook_add(UC_HOOK_CODE,hook)
def point(address): return struct.unpack('<HH',cpu.mem_read(address,4))
def source(): return point(r.SEARCH+0x70)
def entry():
    x,y=point(argument()); events.append((0,x,y,0)); cpu.reg_write(UC_X86_REG_EAX,x&1)
def cell_lookup():
    cpu.mem_write(ROOT+0x24,bytes(cpu.mem_read(argument(),4)))
    cpu.reg_write(UC_X86_REG_EAX,ROOT)
def reachable():
    level=argument(1); output=argument(2); x,y=point(argument()+0x24)
    events.append((1,x,y,level))
    answer,regions,mutate=state['answers'][level]
    r.word(output+4,REPORTS+level*0x100); r.word(output+8,8); r.word(output+0x10,len(regions))
    cpu.mem_write(output+0xd,b'\0') # The supplied region vector is not owned.
    for i,value in enumerate(regions): cpu.mem_write(REPORTS+level*0x100+i*2,struct.pack('<H',value))
    if mutate:
        cpu.mem_write(r.SEARCH+0x70,struct.pack('<HH',x^1,y))
        other=(level+1)%3
        old=struct.unpack('<H',cpu.mem_read(GRID+other*2,2))[0]
        new=(old+1)%(len(state['layers'][other][0])+1)
        cpu.mem_write(GRID+other*2,struct.pack('<H',new))
    cpu.reg_write(UC_X86_REG_EAX,int(answer))
bind(0x54fab0 if base else 0x56d3f0,4,entry)
bind(0x548070 if base else 0x5657a0,4,cell_lookup)
bind(0x565eb0 if base else 0x5840c0,16,reachable)

def inputs(seed):
    value=seed^0xa39265c1
    def next_value():
        nonlocal value
        value=(value*1664525+1013904223)&0xffffffff
        return value>>16
    layers=[]; grid=[[],[]]; answers=[]
    for level in range(3):
        fixture=r.inputs(seed*3+level); layers.append(fixture)
        nodes,_,_,current,_,_=fixture
        grid[0].append(current); grid[1].append((current+1)%(len(nodes)+1))
        answer=bool(next_value()%2)
        regions=[next_value()%(len(nodes)+1) for _ in range(next_value()%8)]
        mutate=bool(next_value()%2)
        answers.append((answer,regions,mutate))
    return dict(layers=layers,grid=grid,answers=answers)

def run(seed):
    global state,events
    state=inputs(seed); events=[]
    cpu.mem_write(r.SEARCH,bytes(0x1000)); cpu.mem_write(GRID,bytes(20))
    cpu.mem_write(r.SEARCH+0x70,struct.pack('<HH',54,69))
    cpu.mem_write(r.SEARCH+0x38,bytes([state['layers'][0][4]]))
    r.word(0x83254c if base else 0x87f858,GRID)
    for i,row in enumerate(state['grid']):
        for level,value in enumerate(row): cpu.mem_write(GRID+i*10+level*2,struct.pack('<H',value))
    for level,fixture in enumerate(state['layers']):
        nodes,_,path,_,_,blocked=fixture
        node_base=NODES+level*0x1000; link_base=LINKS+level*0x1000
        for i,node in enumerate(nodes):
            address=node_base+i*0x24
            r.word(address+4,link_base+i*0x100); r.word(address+0x10,len(node))
            for j,(target,diagonal) in enumerate(node):
                r.word(link_base+i*0x100+j*8,target); r.word(link_base+i*0x100+j*8+4,int(diagonal))
        r.word((0x83256c if base else 0x87f878)+level*24,node_base)
        for i,value in enumerate(path): cpu.mem_write(r.SEARCH+0xbc+level*1000+i*2,struct.pack('<H',value))
        r.word(r.SEARCH+0xc74+level*4,len(path))
        vector=r.SEARCH+0x74+level*24
        r.word(vector+4,EDGES+level*0x1000); r.word(vector+8,1024); r.word(vector+0x10,len(blocked))
        for i,(a,b) in enumerate(blocked): r.word(EDGES+level*0x1000+i*4,(a<<16)|b)
    cpu.mem_write(r.STACK,struct.pack('<II',r.STOP,0x12345678))
    cpu.reg_write(UC_X86_REG_ECX,r.SEARCH); cpu.reg_write(UC_X86_REG_ESP,r.STACK)
    cpu.emu_start(0x42a590 if base else 0x42ccd0,r.STOP,count=100000)
    h=2166136261
    def mix(value):
        nonlocal h
        h=((h^value)*16777619)&0xffffffff
    mix(0) # RetryEdgesStatus::Complete.
    mix(cpu.mem_read(r.SEARCH+0x38,1)[0])
    for level in range(3):
        count=r.read(r.SEARCH+0x84+level*24); mix(count); assert count<=1024
        for i in range(count): mix(r.read(EDGES+level*0x1000+i*4))
    for event in events:
        for value in event: mix(value)
    for value in source(): mix(value)
    for entry in range(2):
        for level in range(3): mix(struct.unpack('<H',cpu.mem_read(GRID+entry*10+level*2,2))[0])
    return h

total=2166136261
for seed in range(4096):
    digest=run(seed)
    if '--vectors' in sys.argv: print(seed,f'{digest:08x}')
    total=((total^digest)*16777619)&0xffffffff
print('4096 original hierarchy retry dispatch cases',f'{total:08x}',file=sys.stderr)
