#!/usr/bin/env python3
"""Run the complete original three-level builder with bounded storage adapters.
Only usable-area answers, allocation/free, and fixed bucket storage are supplied.
All fill decisions, node metadata, and final neighbor insertion execute original x86.
"""
import struct
import sys
import original_hierarchy_check as h
from unicorn import UC_HOOK_CODE,UcError
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
r=h.r; cpu=r.cpu; base=r.base
NODES,POOL=0x1900000,0x1a00000
allocation=POOL
cpu.mem_map(0,0x1000)

def return_to_caller(value=None):
    esp=cpu.reg_read(UC_X86_REG_ESP)
    if value is not None: cpu.reg_write(UC_X86_REG_EAX,value)
    cpu.reg_write(UC_X86_REG_EIP,struct.unpack('<I',cpu.mem_read(esp,4))[0])
    cpu.reg_write(UC_X86_REG_ESP,esp+4)

def allocate(cpu,address,size,data):
    global allocation
    esp=cpu.reg_read(UC_X86_REG_ESP)
    size=struct.unpack('<I',cpu.mem_read(esp+4,4))[0]
    result=allocation; allocation+=(size+15)&~15
    assert allocation<0x2000000,'builder storage adapter bound exceeded'
    return_to_caller(result)
def free(cpu,address,size,data): return_to_caller()
def clear_bucket(cpu,address,size,data):
    r.word(cpu.reg_read(UC_X86_REG_ECX)+0x10,0)
    return_to_caller()
cpu.hook_add(UC_HOOK_CODE,allocate,begin=0x782017 if base else 0x7c8e17,end=0x782017 if base else 0x7c8e17)
cpu.hook_add(UC_HOOK_CODE,free,begin=0x781d3b if base else 0x7c8b3d,end=0x781d3b if base else 0x7c8b3d)
r.word(r.VT+12,0x1008120)
cpu.hook_add(UC_HOOK_CODE,clear_bucket,begin=0x1008120,end=0x1008120)

def run(seed):
    global allocation
    allocation=POOL
    for bucket in range(256): r.word(r.BUCKETS+bucket*24+4,r.DATA+bucket*r.CAPACITY*8)
    r.run(seed)
    h.stride,cells=r.inputs(seed)
    h.usable_cells=[c!=7 and (seed%5!=0 or i%7!=0) for i,(c,z) in enumerate(cells)]
    shift=4 if base else 0
    r.word(r.MAP+0x68-shift,r.GRID); r.word(r.MAP+0x6c-shift,len(cells)); r.word(r.MAP+0x70-shift,h.META)
    real_map=0x8324e0 if base else 0x87f7e8
    r.word(real_map+0xf4-shift,(h.stride-1)//2); r.word(real_map+0xf8-shift,h.stride-1-(h.stride-1)//2)
    cpu.mem_write(h.META,bytes(len(cells)*10))
    for level in (2,1,0):
        r.word(r.MAP+0x80-shift+level*4,r.DESC)
        vector=r.MAP+0x8c-shift+level*24
        nodes=NODES+level*0x10000
        cpu.mem_write(nodes,bytes(512*36))
        r.word(vector,r.VT); r.word(vector+4,nodes); r.word(vector+8,512)
        r.word(vector+12,0x101); r.word(vector+16,0); r.word(vector+20,10)
        for i in range(512):
            node=nodes+i*36
            r.word(node,0x7a5ad8 if base else 0x7ed5a0)
            r.word(node+12,1); r.word(node+20,10)
    output=[]
    for level in (2,1,0):
        for bucket in range(256):
            at=r.BUCKETS+bucket*24
            r.word(at+4,r.DATA+bucket*r.CAPACITY*12); r.word(at+8,r.CAPACITY)
            r.word(at+12,0x101); r.word(at+16,0)
        cpu.reg_write(UC_X86_REG_ECX,r.MAP); cpu.reg_write(UC_X86_REG_ESP,r.STACK)
        cpu.mem_write(r.STACK,struct.pack('<2I',r.STOP,level))
        try: cpu.emu_start(0x563d80 if base else 0x581f90,r.STOP,count=10000000)
        except UcError:
            print('unresolved original builder dependency at',hex(cpu.reg_read(UC_X86_REG_EIP))); raise
        assert cpu.reg_read(UC_X86_REG_EIP)==r.STOP,'original builder instruction bound exceeded'
        labels=[struct.unpack('<H',cpu.mem_read(h.META+j*10+level*2,2))[0] for j in range(len(cells))]
        vector=r.MAP+0x8c-shift+level*24
        nodes=r.signed(vector+4); count=r.signed(vector+16); links=0
        assert 1<=count<=512
        value=2166136261
        def add(item):
            nonlocal value
            value=((value^(item&0xffffffff))*16777619)&0xffffffff
        for label in labels: add(label)
        add(count)
        for i in range(count):
            node=nodes+i*36
            parent=struct.unpack('<H',cpu.mem_read(node+24,2))[0]
            add(parent); add(r.signed(node+28))
            if i: add(r.signed(node+32))
            neighbors=r.signed(node+16); add(neighbors); links+=neighbors
            items=r.signed(node+4)
            for j in range(neighbors):
                neighbor,flag=struct.unpack('<IB',cpu.mem_read(items+j*8,5))
                add(neighbor); add(flag)
        output.append((value,count,links))
    return output

if __name__=='__main__':
    for seed in range(256):
        result=run(seed)
        if '--vectors' in sys.argv: print('{%d,{{%s}}},'%(seed,','.join('{0x%08xu,%d,%d}'%values for values in result)))
        else: print('original full hierarchy builder',seed,result)
