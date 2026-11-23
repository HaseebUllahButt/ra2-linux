#!/usr/bin/env python3
"""Check the original initial coarse-region scan and ordered edges, before tube additions."""
import struct
import sys
from original_image import load_original
from unicorn import UcError
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
cpu=load_original(sys.argv[1]); base='--base' in sys.argv
cpu.mem_map(0x1000000,0x1000000)
MAP,GRID,DESC,BUCKETS,VT,DATA=0x1000000,0x1010000,0x1003000,0x1020000,0x1008000,0x1100000
STACK,STOP,SKIP=0x100f000,0x100e000,0x1003010
CAPACITY=512

def word(address,value): cpu.mem_write(address,struct.pack('<I',value&0xffffffff))
def signed(address): return struct.unpack('<i',cpu.mem_read(address,4))[0]
word(MAP+0x14,DESC); word(DESC,BUCKETS)
word(VT+8,0x1008100); cpu.mem_write(0x1008100,b'\xcc') # Fail if the explicit storage bound is insufficient.
for bucket in range(256):
    at=BUCKETS+bucket*24
    word(at,VT); word(at+4,DATA+bucket*CAPACITY*8); word(at+8,CAPACITY)
    word(at+0xc,0x101); word(at+0x14,10)

def inputs(seed):
    n=9+2*(seed%3); w=(n-1)//2; h=n-1-w
    state=(0x9e3779b9^seed)&0xffffffff
    cells=[]
    for y in range(n):
        for x in range(n):
            state=(state*1664525+1013904223)&0xffffffff; category=(state>>24)&7
            state=(state*1664525+1013904223)&0xffffffff; height=(state>>16)&7
            inside=(x+y>w and x-y<w and y-x<w and x+y<=w+2*h)
            if seed==0: category,height=0,0
            elif seed==1: category,height=x%4,0
            elif seed==2: category,height=(x+y)%2,0
            elif seed==3: category,height=0,(x+2*y)%8
            elif seed==4: category,height=6,(x*3+y*5)&255
            elif seed==5: category,height=7,0
            if not inside: category,height=7,0
            cells.append((category,height))
    return n,cells

def run(seed):
    n,cells=inputs(seed)
    # Only the stride is used by this recovered component helper.
    word(MAP+(0xf0 if base else 0xf4),(n-1)//2)
    word(MAP+(0xf4 if base else 0xf8),n-1-(n-1)//2)
    cpu.mem_write(GRID,b''.join(struct.pack('<BBH',c,h,0) for c,h in cells))
    for bucket in range(256): word(BUCKETS+bucket*24+0x10,0)
    categories=[7]; sizes=[0]; largest=0xffffffff; maximum=-1; i=0
    while i<len(cells):
        label=struct.unpack('<H',cpu.mem_read(GRID+i*4+2,2))[0]
        if cells[i][0]==7 or label: i+=1; continue
        group=len(categories); categories.append(cells[i][0])
        word(0xa6fc34 if base else 0xabde8c,0)
        cpu.reg_write(UC_X86_REG_ECX,MAP); cpu.reg_write(UC_X86_REG_ESP,STACK)
        cpu.mem_write(STACK,struct.pack('<4I',STOP,GRID+i*4,group,SKIP))
        try: cpu.emu_start(0x54f250 if base else 0x56cb90,STOP,count=10000000)
        except UcError:
            print('unresolved original region dependency at',hex(cpu.reg_read(UC_X86_REG_EIP))); raise
        assert cpu.reg_read(UC_X86_REG_EIP)==STOP,'original region instruction bound exceeded'
        count=cpu.reg_read(UC_X86_REG_EAX); count=count if count<0x80000000 else count-0x100000000
        sizes.append(count)
        if maximum<count: maximum=count; largest=group
        i+=signed(SKIP)
    labels=[struct.unpack('<H',cpu.mem_read(GRID+j*4+2,2))[0] for j in range(len(cells))]
    edges=[]
    for bucket in range(256):
        at=BUCKETS+bucket*24; count=signed(at+0x10)
        assert 0<=count<CAPACITY
        for j in range(count):
            pair=struct.unpack('<II',cpu.mem_read(DATA+bucket*CAPACITY*8+j*8,8))
            assert pair[0]==pair[1]
            edges.append(pair[0])
    h=2166136261
    for value in (*labels,len(categories),*categories,*sizes,largest,len(edges),*edges): h=((h^(value&0xffffffff))*16777619)&0xffffffff
    return h,len(categories),len(edges)

if __name__=='__main__':
    for seed in range(256):
        h,groups,edges=run(seed)
        if '--vectors' in sys.argv: print('{%d,0x%08xu,%d,%d},'%(seed,h,groups,edges))
        else: print('original coarse region',seed,'%08X'%h,groups,edges)
