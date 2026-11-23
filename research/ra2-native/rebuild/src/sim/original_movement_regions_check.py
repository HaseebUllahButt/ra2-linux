#!/usr/bin/env python3
"""Run the complete original coarse-region and movement-component builder and ground lookup."""
import struct
import sys
import original_hierarchy_builder_check as b
from unicorn import UcError
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
r=b.r; cpu=r.cpu; base=r.base
POINT=0x1810100

def coordinates(n):
    return ((-5,-5),(-1,0),(0,-1),(0,0),(1,1),(n-1,n-1),(n,n),
            (n+5,1),(-n+5,3),(-1,n),(32767,32767),(-32768,-32768),(5,5),(-1,1))

def invoke(address,*args):
    cpu.reg_write(UC_X86_REG_ECX,r.MAP); cpu.reg_write(UC_X86_REG_ESP,r.STACK)
    cpu.mem_write(r.STACK,struct.pack('<'+'I'*(len(args)+1),r.STOP,*args))
    try: cpu.emu_start(address,r.STOP,count=10000000)
    except UcError:
        print('unresolved original movement-region dependency at',hex(cpu.reg_read(UC_X86_REG_EIP))); raise
    assert cpu.reg_read(UC_X86_REG_EIP)==r.STOP,'original movement-region instruction bound exceeded'
    return cpu.reg_read(UC_X86_REG_EAX)

def run(seed):
    b.allocation=b.POOL
    n,cells=r.inputs(seed); shift=4 if base else 0
    r.word(r.MAP+0xf4-shift,(n-1)//2); r.word(r.MAP+0xf8-shift,n-1-(n-1)//2)
    r.word(r.MAP+0x68-shift,r.GRID); r.word(r.MAP+0x6c-shift,len(cells)); r.word(r.MAP+0x60-shift,0)
    r.word(r.DESC+8,256)
    cpu.mem_write(r.GRID,b''.join(struct.pack('<BBH',category,height,0) for category,height in cells))
    for bucket in range(256):
        at=r.BUCKETS+bucket*24
        r.word(at+4,r.DATA+bucket*r.CAPACITY*8); r.word(at+8,r.CAPACITY)
        r.word(at+12,0x101); r.word(at+16,0)
    largest=invoke(0x54ebd0 if base else 0x56c510)
    count=r.signed(r.MAP+0x4c-shift)
    assert 1<=count<65536
    labels=[struct.unpack('<H',cpu.mem_read(r.GRID+i*4+2,2))[0] for i in range(len(cells))]
    edges=[]
    for bucket in range(256):
        length=r.signed(r.BUCKETS+bucket*24+16)
        assert 0<=length<r.CAPACITY
        for i in range(length):
            pair,copy=struct.unpack('<II',cpu.mem_read(r.DATA+bucket*r.CAPACITY*8+i*8,8))
            assert pair==copy
            edges.append(pair)
    value=2166136261
    def add(item):
        nonlocal value
        value=((value^(item&0xffffffff))*16777619)&0xffffffff
    add(count); add(largest)
    for label in labels: add(label)
    add(len(edges))
    for edge in edges: add(edge)
    for zone in range(12 if base else 13):
        items=r.signed(r.MAP+0x18+zone*4)
        for i in range(count): add(struct.unpack('<H',cpu.mem_read(items+i*2,2))[0])
        for x,y in coordinates(n):
            cpu.mem_write(POINT,struct.pack('<hh',x,y))
            add(invoke(0x54f8f0 if base else 0x56d230,POINT,zone,0))
    return value,count,len(edges)

if __name__=='__main__':
    for seed in range(256):
        value,count,edges=run(seed)
        if '--vectors' in sys.argv: print('{%d,0x%08xu,%d,%d},'%(seed,value,count,edges))
        else: print('original movement regions',seed,'%08X'%value,count,edges)
