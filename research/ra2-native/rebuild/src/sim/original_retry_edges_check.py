#!/usr/bin/env python3
"""Check original hierarchy retry edge selection, including its append body.

Usage: original_retry_edges_check.py executable [--base] [--vectors]
The graph, path, and initialized vector storage are explicit fixture inputs.
Both original functions execute unchanged. No callbacks or stubs are used.
"""
import struct
import sys
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP
from original_image import load_original
cpu=load_original(sys.argv[1]); base='--base' in sys.argv
cpu.mem_map(0x1000000,0x10000)
SEARCH,NODES,LINKS,EDGES,STACK,STOP=0x1000000,0x1002000,0x1004000,0x1006000,0x100f000,0x100e000
def word(address,value): cpu.mem_write(address,struct.pack('<I',value&0xffffffff))
def read(address): return struct.unpack('<I',cpu.mem_read(address,4))[0]

def inputs(seed):
    value=seed^0x96c4a31f
    def next_value():
        nonlocal value
        value=(value*1664525+1013904223)&0xffffffff
        return value>>16
    count=next_value()%11+2; level=next_value()%3; nodes=[]
    for _ in range(count):
        links=[]
        for _ in range(next_value()%12):
            links.append((next_value()%count,bool(next_value()%2)))
        nodes.append(links)
    path=[next_value()%count for _ in range(next_value()%13)]
    current=next_value()%(count+1); allowed=bool(next_value()%2)
    blocked=[(next_value()%count,next_value()%count) for _ in range(next_value()%4)]
    return nodes,level,path,current,allowed,blocked

def run(seed):
    nodes,level,path,current,allowed,blocked=inputs(seed)
    cpu.mem_write(SEARCH,bytes(0x8000))
    cpu.mem_write(SEARCH+0x38,bytes([allowed]))
    for i,node in enumerate(nodes):
        address=NODES+i*0x24
        word(address+4,LINKS+i*0x100); word(address+0x10,len(node))
        for j,(target,diagonal) in enumerate(node):
            word(LINKS+i*0x100+j*8,target); word(LINKS+i*0x100+j*8+4,int(diagonal))
    word((0x83256c if base else 0x87f878)+level*24,NODES)
    for i,value in enumerate(path): cpu.mem_write(SEARCH+0xbc+level*1000+i*2,struct.pack('<H',value))
    word(SEARCH+0xc74+level*4,len(path))
    vector=SEARCH+0x74+level*24
    word(vector+4,EDGES); word(vector+8,1024); word(vector+0x10,len(blocked))
    for i,(a,b) in enumerate(blocked): word(EDGES+i*4,(a<<16)|b)
    cpu.mem_write(STACK,struct.pack('<III',STOP,current,level))
    cpu.reg_write(UC_X86_REG_ECX,SEARCH); cpu.reg_write(UC_X86_REG_ESP,STACK)
    cpu.emu_start(0x42a840 if base else 0x42cf80,STOP,count=100000)
    h=2166136261
    def mix(value):
        nonlocal h
        h=((h^value)*16777619)&0xffffffff
    mix(cpu.mem_read(SEARCH+0x38,1)[0]); count=read(vector+0x10); mix(count)
    assert count<=1024
    for i in range(count): mix(read(EDGES+i*4))
    return h

if __name__=='__main__':
    total=2166136261
    for seed in range(4096):
        digest=run(seed)
        if '--vectors' in sys.argv: print(seed,f'{digest:08x}')
        total=((total^digest)*16777619)&0xffffffff
    print('4096 original hierarchy retry cases',f'{total:08x}',file=sys.stderr)
