#!/usr/bin/env python3
"""Run the original monotone smoothing pass and its ordered virtual-entry queries."""
import struct
import sys
import original_route_check as r
from unicorn import UcError
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
m=r.m; cpu=m.cpu; base=m.base
DESCRIPTOR,PATH,HEIGHTS=0x100d000,0x100d100,0x100d300
seed_value=0; queries=[]

def reply(x,y,seed):
    status=7 if seed%4!=0 and (x+2*y+seed)%11==0 else 0
    marked=seed%4!=0 and (2*x+y+seed)%13==0
    level=(x+y+seed)%5-2 if seed%3 else 0
    bridge=(x+seed)%2!=0
    return status,marked,level,bridge

def lookup():
    pair=m.read(m.argument()); x,y=pair&65535,pair>>16
    cell=m.cell_for(x,y); status,marked,level,bridge=reply(x,y,seed_value)
    cpu.mem_write(cell+0x11b,struct.pack('<b',level))
    m.word(cell+0x140,(0x100 if bridge else 0)|(0x40000 if marked else 0))
    cpu.reg_write(UC_X86_REG_EAX,cell)
m.callbacks[0x548070 if base else 0x5657a0]=lookup

def can_enter():
    cell=m.argument(); pair=m.read(cell+0x24); x,y=pair&65535,pair>>16
    direction=m.argument(1); layer=m.argument(2); layer=layer if layer<0x80000000 else layer-0x100000000
    assert m.argument(3)==0 and m.argument(4)==1
    queries.append((x,y,direction,layer))
    cpu.reg_write(UC_X86_REG_EAX,reply(x,y,seed_value)[0])

def inputs(seed):
    state=seed^0xb8a72e91
    path=[]
    for i in range(2+seed%27):
        state=(state*1664525+1013904223)&0xffffffff
        path.append(((seed%4)*2+((state>>16)%3))%8)
    if seed==0: path=[0,1,2]
    if seed==1: path=[2,2,0,0]
    if seed==2: path=[1]*25
    return path

def run(seed):
    global seed_value,queries
    seed_value=seed; queries=[]; r.setup((0,),15)
    m.virtual(0x194 if base else 0x1ac,20,can_enter)
    cpu.mem_write(m.FOOT+(0x428 if base else 0x530),struct.pack('<d',0.0))
    path=inputs(seed); x,y=54,69; layers=[]
    for direction in path:
        layers.append(reply(x,y,seed)[2]+(4 if seed%5==0 else 0))
        x+=r.DIRECTIONS[direction][0]; y+=r.DIRECTIONS[direction][1]
    cpu.mem_write(DESCRIPTOR,struct.pack('<6I',54|(69<<16),0,len(path)+1,PATH,0,HEIGHTS))
    cpu.mem_write(PATH,struct.pack('<'+'i'*(len(path)+2),*path,-1,-1))
    cpu.mem_write(HEIGHTS,struct.pack('<'+'i'*len(layers),*layers))
    cpu.reg_write(UC_X86_REG_ECX,0); cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    cpu.mem_write(0x103f000,struct.pack('<3I',0x103e000,DESCRIPTOR,m.FOOT))
    try: cpu.emu_start(0x4290c0 if base else 0x42b7f0,0x103e000,count=1000000)
    except UcError:
        print('unresolved original smoothing dependency',hex(cpu.reg_read(UC_X86_REG_EIP)),[hex(a) for a in m.recent]); raise
    assert cpu.reg_read(UC_X86_REG_EIP)==0x103e000,'original smoothing instruction bound exceeded'
    count=m.read(DESCRIPTOR+8)-1
    assert count<=len(path)
    output=struct.unpack('<'+'i'*count,cpu.mem_read(PATH,count*4)) if count else ()
    assert all(0<=direction<=7 for direction in output)
    value=2166136261
    for item in (count,*output,len(queries),*[item for query in queries for item in query]): value=((value^(item&0xffffffff))*16777619)&0xffffffff
    return value,count,len(queries)

if __name__=='__main__':
    for seed in range(256):
        value,count,query_count=run(seed)
        if '--vectors' in sys.argv: print('{%d,0x%08xu,%d,%d},'%(seed,value,count,query_count))
        else: print('original monotone smoothing',seed,'%08X'%value,count,query_count)
