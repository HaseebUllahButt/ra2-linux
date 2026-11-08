#!/usr/bin/env python3
"""Check the original three-level route search with no actor threat contribution.
The original full map hierarchy builder supplies node order and neighbors.
"""
import struct
import sys
import original_hierarchy_builder_check as b
from unicorn import UcError
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_FPCW
r=b.r; h=b.h; cpu=r.cpu; base=r.base
SEARCH,HEAP,ITEMS,SCRATCH,ARRAYS=0x1840000,0x1842000,0x1843000,0x1850000,0x1880000
START,END=0x1810040,0x1810044
cpu.reg_write(UC_X86_REG_FPCW,0xe7f)

def endpoints(seed,cells):
    available=[i for i,(category,height) in enumerate(cells) if category!=7]
    if not available: available=[0]
    first,last,middle=available[0],available[-1],available[len(available)//2]
    return ((first,last),(last,first),(middle,middle),(first,middle))

def run_search(seed,zone,source,destination):
    real_map=0x8324e0 if base else 0x87f7e8; shift=4 if base else 0
    cpu.mem_write(SEARCH,bytes(0x1000)); cpu.mem_write(HEAP,bytes(20))
    r.word(HEAP+4,10000); r.word(HEAP+8,ITEMS); r.word(HEAP+16,0xffffffff)
    r.word(SEARCH+0x28,1); r.word(SEARCH+0x68,HEAP); r.word(SEARCH+0x64,SCRATCH)
    counts=[]
    r.word(real_map+0x70-shift,h.META); r.word(real_map+0x6c-shift,h.stride*h.stride)
    for level in (2,1,0):
        vector=r.MAP+0x8c-shift+level*24
        count=r.signed(vector+16); nodes=r.signed(vector+4); counts.append(count)
        r.word(real_map+0x90-shift+level*24,nodes)
        arrays=ARRAYS+level*0x4000
        cpu.mem_write(arrays,bytes(0x3000))
        r.word(SEARCH+0x40+level*4,arrays); r.word(SEARCH+0x4c+level*4,arrays+0x1000); r.word(SEARCH+0x58+level*4,arrays+0x2000)
        if seed%7==0 and count>1 and r.signed(nodes+36+16)>0:
            neighbor=r.signed(r.signed(nodes+36+4))
            r.word(arrays+0x3000,(min(1,neighbor)<<16)|max(1,neighbor))
            r.word(SEARCH+0x78+level*24,arrays+0x3000); r.word(SEARCH+0x84+level*24,1)
    r.word(START,(source%h.stride)|((source//h.stride)<<16)); r.word(END,(destination%h.stride)|((destination//h.stride)<<16))
    cpu.reg_write(UC_X86_REG_ECX,SEARCH); cpu.reg_write(UC_X86_REG_ESP,r.STACK)
    cpu.mem_write(r.STACK,struct.pack('<5I',r.STOP,START,END,zone,0))
    try: cpu.emu_start(0x429b60 if base else 0x42c290,r.STOP,count=10000000)
    except UcError:
        print('unresolved hierarchy route dependency at',hex(cpu.reg_read(UC_X86_REG_EIP))); raise
    assert cpu.reg_read(UC_X86_REG_EIP)==r.STOP,'original hierarchy route instruction bound exceeded'
    found=cpu.reg_read(UC_X86_REG_EAX)&255
    values=[found]
    for level,count in zip((2,1,0),counts):
        length=r.signed(SEARCH+0xc74+level*4)
        assert 0<=length<=500
        path=list(struct.unpack('<'+'H'*length,cpu.mem_read(SEARCH+0xbc+level*1000,length*2))) if length else []
        values.extend((length,*path))
        arrays=ARRAYS+level*0x4000
        for i in range(count): values.append(int(r.signed(arrays+i*4)==1))
        for i in range(count):
            visited=int(r.signed(arrays+0x1000+i*4)==1); values.append(visited)
            if visited: values.append(struct.unpack('<I',cpu.mem_read(arrays+0x2000+i*4,4))[0])
    value=2166136261
    for item in values: value=((value^(item&0xffffffff))*16777619)&0xffffffff
    return value

if __name__=='__main__':
    for seed in range(256):
        b.run(seed); stride,cells=r.inputs(seed); value=2166136261
        for zone in range(12 if base else 13):
            for source,destination in endpoints(seed,cells):
                value=((value^run_search(seed,zone,source,destination))*16777619)&0xffffffff
        if '--vectors' in sys.argv: print('{%d,0x%08xu},'%(seed,value))
        else: print('original hierarchy routes',seed,'%08X'%value)
