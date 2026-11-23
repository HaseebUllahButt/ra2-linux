#!/usr/bin/env python3
"""Run the original bounded hierarchy fill and retain its ordered, flagged links.
The driver supplies resolved coarse records, usable-area answers, and fixed vector storage.
"""
import struct
import sys
import original_region_check as r
from unicorn import UC_HOOK_CODE,UcError
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
cpu=r.cpu; base=r.base
META,RECT,POINT=0x1800000,0x1810000,0x1810020
usable_cells=[]; stride=0

def usable(cpu,address,size,data):
    esp=cpu.reg_read(UC_X86_REG_ESP)
    point=struct.unpack('<I',cpu.mem_read(esp+4,4))[0]
    x,y=struct.unpack('<hh',cpu.mem_read(point,4))
    assert 0<=x<stride and 0<=y<stride,(x,y)
    cpu.reg_write(UC_X86_REG_EAX,int(usable_cells[y*stride+x]))
    cpu.reg_write(UC_X86_REG_ESP,esp+12)
    cpu.reg_write(UC_X86_REG_EIP,struct.unpack('<I',cpu.mem_read(esp,4))[0])
cpu.hook_add(UC_HOOK_CODE,usable,begin=0x55a660 if base else 0x578460,end=0x55a660 if base else 0x578460)

def run(seed):
    global stride,usable_cells
    for bucket in range(256): r.word(r.BUCKETS+bucket*24+4,r.DATA+bucket*r.CAPACITY*8)
    r.run(seed)
    stride,cells=r.inputs(seed)
    raw=cpu.mem_read(r.GRID,len(cells)*4)
    raw_labels=[struct.unpack_from('<H',raw,j*4+2)[0] for j in range(len(cells))]
    usable_cells=[c!=7 and (seed%5!=0 or i%7!=0) for i,(c,h) in enumerate(cells)]
    meta=bytearray(len(cells)*10)
    for i,(c,h) in enumerate(cells): struct.pack_into('<HB',meta,i*10+6,raw_labels[i],h)
    cpu.mem_write(META,bytes(meta))
    output=[]
    for level in (2,1,0):
        r.word(r.MAP+(0x7c if base else 0x80)+level*4,r.DESC)
        for bucket in range(256):
            at=r.BUCKETS+bucket*24
            r.word(at+4,r.DATA+bucket*r.CAPACITY*12); r.word(at+0x10,0)
        nodes=[(0,7,None)]; i=0; block=1<<(level+1)
        while i<len(cells):
            label=struct.unpack('<H',cpu.mem_read(META+i*10+level*2,2))[0]
            if cells[i][0]==7 or label: i+=1; continue
            x,y=i%stride,i//stride
            r.word(RECT,x-(x&(block-1))); r.word(RECT+4,y-(y&(block-1)))
            r.word(RECT+8,block); r.word(RECT+12,block)
            cpu.mem_write(POINT,struct.pack('<hh',x,y))
            cpu.reg_write(UC_X86_REG_ECX,r.MAP); cpu.reg_write(UC_X86_REG_ESP,r.STACK)
            cpu.mem_write(r.STACK,struct.pack('<6I',r.STOP,META+i*10,level,len(nodes),RECT,POINT))
            try: cpu.emu_start(0x564290 if base else 0x5824a0,r.STOP,count=10000000)
            except UcError:
                print('unresolved hierarchy dependency at',hex(cpu.reg_read(UC_X86_REG_EIP))); raise
            assert cpu.reg_read(UC_X86_REG_EIP)==r.STOP,'original hierarchy instruction bound exceeded'
            skip=cpu.reg_read(UC_X86_REG_EAX); skip=skip if skip<0x80000000 else skip-0x100000000
            parent=struct.unpack('<H',cpu.mem_read(META+i*10+(level+1)*2,2))[0] if level<2 else 0
            nodes.append((parent,cells[i][0],x//4+0x83+(y//4)*0x82))
            i+=skip
        links=[[] for _ in nodes]
        for bucket in range(256):
            count=r.signed(r.BUCKETS+bucket*24+0x10)
            assert 0<=count<r.CAPACITY
            for j in range(count):
                pair,copy,flag=struct.unpack('<IIB',cpu.mem_read(r.DATA+bucket*r.CAPACITY*12+j*12,9))
                assert pair==copy and flag in (0,1)
                neighbor,current=pair>>16,pair&65535
                links[current].append((neighbor,flag)); links[neighbor].append((current,flag))
        labels=[struct.unpack('<H',cpu.mem_read(META+j*10+level*2,2))[0] for j in range(len(cells))]
        h=2166136261
        def add(value):
            nonlocal h
            h=((h^(value&0xffffffff))*16777619)&0xffffffff
        for label in labels: add(label)
        add(len(nodes))
        for (parent,category,center),neighbors in zip(nodes,links):
            add(parent); add(category)
            if center is not None: add(center)
            add(len(neighbors))
            for neighbor,flag in neighbors: add(neighbor); add(flag)
        output.append((h,len(nodes),sum(map(len,links))))
    return output

if __name__=='__main__':
    for seed in range(256):
        result=run(seed)
        if '--vectors' in sys.argv: print('{%d,{{%s}}},'%(seed,','.join('{0x%08xu,%d,%d}'%values for values in result)))
        else: print('original hierarchy',seed,result)
