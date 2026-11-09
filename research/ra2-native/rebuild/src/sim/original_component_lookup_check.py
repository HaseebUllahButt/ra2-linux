#!/usr/bin/env python3
"""Run original component lookup and every bridge helper without replacement bodies.

Usage: original_component_lookup_check.py executable [--base] [--vectors|--transition-vectors]
The original transition scan, neighbor helper, Map lookup, tile-range getters,
clamped region read, and movement-table read all execute. Raw state is supplied.
"""
import struct
import sys
from original_image import load_original
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDI,UC_X86_REG_ESI,UC_X86_REG_ESP,UC_X86_REG_EIP

def point(x,y):return (x&65535,y&65535)
def packed(p):return p[0]|p[1]<<16
def signed(v):return v if v<32768 else v-65536
def add(p,x,y):return point(p[0]+x,p[1]+y)
def transition_inputs(seed):
    state=seed^0x18d6a79b
    def next_value():
        nonlocal state
        state=(state*1664525+1013904223)&0xffffffff;return state>>16
    p=point(next_value(),next_value());tolerance=next_value()%5-1;start=next_value()%5;records=[]
    for i in range(6):
        first=add(p,next_value()%9-4,next_value()%9-4);second=add(first,next_value()%7-3,next_value()%7-3)
        if seed%4==0 or i==3:
            first=add(p,-1,0) if seed&4 else add(p,0,-1)
            second=add(p,2,seed%7-3) if seed&4 else add(p,0,2)
        records.append((first,second,next_value(),1 if next_value()%4==0 else 0))
    if seed%17==0:start=9
    return p,tolerance,start,records

def component_inputs(seed,base):
    profile=seed%16;n=9+2*(seed%3);zone_count=12 if base else 13
    q=point(3+seed%3,3+seed//3%3);zone=seed//16%zone_count;above=profile!=0
    if profile==12:q=((65535,1),(512,0),(32767,0),(32768,64))[seed//16%4]
    vertical=not seed&16;first=add(q,0,-1) if vertical else add(q,-1,0);second=add(q,0,2) if vertical else add(q,2,5)
    record=(first,second,255 if profile==3 else 0,0);records=[record]
    if profile==2:records=[]
    if profile==11:records=[(second,first,0,0)]
    if profile==13:records=[(first,second,0,-1),record,record]
    if profile==15:records=[(first,second,0,7)]
    tile=seed%16;land=0;range0=100;range1=200
    if profile in (4,7,12,13):range0=tile
    if profile==5:range1=tile
    if profile==7:land=3
    if profile==8:range0=-1;tile=0
    if profile==9:range0=0;tile=16 if seed&16 else 15
    if profile==10:range1=-1;tile=14
    if profile==14:range0=0x7ffffff8;tile=0x7ffffffc;range1=-1
    raw=q;cells={};length=1+(seed//32)%8
    for step in range(length+1):
        index=signed(raw[1])*512+signed(raw[0])
        if index<0 or index>=262144:break
        actual=point(index%512,index//512);cells[index]=(actual,profile!=1 and step<length,tile,land)
        raw=add(actual,0,1) if vertical else add(actual,1,0)
    labels=[(i*7+seed)%32 for i in range(n*n)]
    components=[[((seed*13+i*37+z*101)&65535) if i else 65535 for i in range(32)] for z in range(zone_count)]
    return dict(point=q,zone=zone,above=above,stride=n,records=records,cells=cells,range0=range0,range1=range1,labels=labels,components=components)

class Oracle:
    VECTOR,RECORDS,POINT,GRID,COMPONENTS,POOL,STACK,STOP=0x1100000,0x1200000,0x1210000,0x1211000,0x1220000,0x1230000,0x13ff000,0x13fe000
    def __init__(self,path,base):
        self.base=base;self.cpu=load_original(path);self.cpu.mem_map(0x1000000,0x400000)
        self.map=0x8324e0 if base else 0x87f7e8;self.fallback=0xa6f9f8 if base else 0xabdc50
        self.record_pointer=0x50 if base else 0x54;self.record_count=0x5c if base else 0x60
        self.word(self.map+(0x138 if base else 0x13c),self.VECTOR)
        self.word(self.map+self.record_pointer,self.RECORDS)
        self.word(self.STACK,self.STOP);self.cpu.reg_write(UC_X86_REG_ESP,self.STACK)
        direction_table=0x851a70 if base else 0x89f688
        self.cpu.mem_write(direction_table,b'\xa5'*32)
        self.cpu.emu_start(0x491620 if base else 0x49f2f0,self.STOP,count=1000)
        assert self.cpu.reg_read(UC_X86_REG_EIP)==self.STOP
        assert struct.unpack('<16h',self.cpu.mem_read(direction_table,32))==(0,-1,1,-1,1,0,1,1,0,1,-1,1,-1,0,-1,-1)
        self.steps=[];self.reads=[];self.selected=None;self.index=None;self.walk=0;self.full=False
        addresses=(0x54f939,0x5500d0,0x54f954,0x54f9a7,0x476ac0,0x5480a5,0x47b960,0x47b980,0x54fa01) if base else (0x56d279,0x56da10,0x56d294,0x56d2e5,0x481810,0x5657d5,0x486750,0x486770,0x56d33f)
        self.addresses=addresses
        for address in addresses:self.cpu.hook_add(UC_HOOK_CODE,self.observe,begin=address,end=address)
        for address in (0x638840,0x6388a0) if base else (0x65c780,0x65c7e0):
            self.cpu.hook_add(UC_HOOK_CODE,self.rng,begin=address,end=address)
    def rng(self,*args):raise AssertionError('component lookup consumed shared RNG')
    def word(self,address,value):self.cpu.mem_write(address,struct.pack('<I',value&0xffffffff))
    def read(self,address):return struct.unpack('<I',self.cpu.mem_read(address,4))[0]
    def argument(self,index=0):return self.read(self.cpu.reg_read(UC_X86_REG_ESP)+4+index*4)
    def record_read(self,raw,address):self.reads.append((raw,self.read(address+0x24),int(bool(self.read(address+0x140)&0x100)),0))
    def observe(self,cpu,address,size,user):
        if not self.full:return
        source,find,found,walk,neighbor,map_ret,range0,range1,ground=self.addresses
        if address==source:
            self.steps.append(0);self.record_read(self.read(cpu.reg_read(UC_X86_REG_ESI)),cpu.reg_read(UC_X86_REG_EAX))
        elif address==find:
            self.steps.append(1);assert self.read(self.argument())==packed(self.data['point']) and self.argument(1)==1 and self.argument(2)==0
        elif address==found:self.index=cpu.reg_read(UC_X86_REG_EAX)
        elif address==walk:
            self.steps.append(2);self.record_read(self.read(cpu.reg_read(UC_X86_REG_ESI)),cpu.reg_read(UC_X86_REG_EDI))
        elif address==neighbor:
            self.steps.append(3);self.walk+=1;assert self.argument() in (2,4)
        elif address==map_ret:
            self.steps.append(2);self.record_read(self.read(self.argument()),cpu.reg_read(UC_X86_REG_EAX))
        elif address==range0:self.steps.append(4)
        elif address==range1:self.steps.append(5)
        elif address==ground:self.steps.append(6);self.selected=self.read(cpu.reg_read(UC_X86_REG_ESI))
    def records(self,records):
        self.word(self.map+self.record_count,len(records))
        raw=b''.join(struct.pack('<4I',packed(a),packed(b),0x932ac000|(flag&255),active&0xffffffff) for a,b,flag,active in records)
        self.cpu.mem_write(self.RECORDS,raw if raw else b'\0'*16);return raw
    def invoke(self,address,*args):
        self.cpu.mem_write(self.STACK,struct.pack('<'+'I'*(len(args)+1),self.STOP,*(arg&0xffffffff for arg in args)))
        self.cpu.reg_write(UC_X86_REG_ESP,self.STACK);self.cpu.reg_write(UC_X86_REG_ECX,self.map)
        self.cpu.emu_start(address,self.STOP,count=100000)
        assert self.cpu.reg_read(UC_X86_REG_EIP)==self.STOP and self.cpu.reg_read(UC_X86_REG_ESP)==self.STACK+4*(len(args)+1)
        return self.cpu.reg_read(UC_X86_REG_EAX)
    def transition(self,seed):
        p,tolerance,start,records=transition_inputs(seed);self.full=False;raw=self.records(records);self.word(self.POINT,packed(p))
        result=self.invoke(0x5500d0 if self.base else 0x56da10,self.POINT,tolerance,start)
        assert self.read(self.POINT)==packed(p) and bytes(self.cpu.mem_read(self.RECORDS,len(raw)))==raw
        return result
    def cell(self,address,metadata):
        actual,bridge,tile,land=metadata
        self.word(address+0x24,packed(actual));self.word(address+0x140,0xf0f0e0ef|(0x100 if bridge else 0))
        self.word(address+0x38,tile);self.word(address+0xec,land)
    def run(self,seed):
        self.data=d=component_inputs(seed,self.base);self.steps=[];self.reads=[];self.selected=None;self.index=None;self.walk=0
        self.cpu.mem_write(self.VECTOR,bytes(262144*4));used=[]
        for index,metadata in d['cells'].items():
            address=self.POOL+len(used)*0x200;self.cell(address,metadata);self.word(self.VECTOR+index*4,address);used.append(address)
        self.cell(self.fallback,((0,0),False,0,0));raw=self.records(d['records']);self.word(self.POINT,packed(d['point']))
        self.word(self.map+(0xf0 if self.base else 0xf4),3);self.word(self.map+(0xf4 if self.base else 0xf8),d['stride']-4)
        self.word(self.map+(0x64 if self.base else 0x68),self.GRID);self.word(self.map+(0x68 if self.base else 0x6c),len(d['labels']))
        self.cpu.mem_write(self.GRID,b''.join(struct.pack('<BBH',0,0,label) for label in d['labels']))
        for z,values in enumerate(d['components']):
            address=self.COMPONENTS+z*64;self.word(self.map+0x18+z*4,address);self.cpu.mem_write(address,struct.pack('<32H',*values))
        self.word(0xa52c74 if self.base else 0xaa0e28,d['range0']);self.word(0xa6cb68 if self.base else 0xabad1c,d['range1'])
        before={address:bytes(self.cpu.mem_read(address,0x150)) for address in used}
        fallback_before=bytes(self.cpu.mem_read(self.fallback,0x150))
        self.full=True;result=self.invoke(0x54f8f0 if self.base else 0x56d230,self.POINT,d['zone'],d['above']);self.full=False
        assert self.read(self.POINT)==packed(d['point']) and bytes(self.cpu.mem_read(self.RECORDS,len(raw)))==raw
        for address,fields in before.items():assert bytes(self.cpu.mem_read(address,0x150))==fields,'component lookup changed a mapped Cell'
        fallback_after=bytes(self.cpu.mem_read(self.fallback,0x150))
        assert fallback_after[:0x24]+fallback_after[0x28:]==fallback_before[:0x24]+fallback_before[0x28:],'component lookup changed fallback fields beyond the lookup coordinate'
        h=2166136261
        values=(result,self.index if self.index is not None else 0xfffffffe,self.selected if self.selected is not None else 0xffffffff,self.walk,len(self.steps),*self.steps,len(self.reads),*(v for row in self.reads for v in row))
        for v in values:h=((h^v)*16777619)&0xffffffff
        return h
if __name__=='__main__':
    oracle=Oracle(sys.argv[1],'--base' in sys.argv);total=2166136261
    transitions='--transition-vectors' in sys.argv;count=4096 if transitions else 2048
    for seed in range(count):
        h=oracle.transition(seed) if transitions else oracle.run(seed)
        if '--vectors' in sys.argv or transitions:print(seed,f'{h:08x}')
        total=((total^h)*16777619)&0xffffffff
    expected=0xc02faa0c if transitions else 0x6f81653d if oracle.base else 0x9b3d78ea
    assert total==expected,'original component lookup vectors changed'
    print(count,'original transition selections' if transitions else 'original complete component lookups',f'{total:08x}',file=sys.stderr)
