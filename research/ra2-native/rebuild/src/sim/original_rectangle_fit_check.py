#!/usr/bin/env python3
"""Run original rectangle fit and Cell fit with real sparse Cell-vector lookup.

Usage: original_rectangle_fit_check.py executable [--base] [--vectors]
Only component answers are supplied. The original inline lookup, fallback,
loop order, overlay rejection, Cell fit body, and land-table reads all execute.
"""
import struct
import sys
from original_cell_fit_check import FitOracle
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP

def signed16(n):return n if n<32768 else n-65536
def pack(x,y):return x&65535|(y&65535)<<16
class RectangleOracle(FitOracle):
    VECTOR,POOL,POINT=0x1100000,0x1200000,0x1003000
    def __init__(self,path,base):
        super().__init__(path,base);self.cpu.mem_map(self.POOL,0x10000)
        self.word(0x832618 if base else 0x87f924,self.VECTOR)
        self.fallback=0xa6f9f8 if base else 0xabdc50
        observer=0x550d62 if base else 0x56e832
        self.cpu.hook_add(UC_HOOK_CODE,self.lookup,begin=observer,end=observer)
    def component(self,cpu,address,size,user):
        esp=cpu.reg_read(UC_X86_REG_ESP);p,zone,above=struct.unpack('<III',cpu.mem_read(esp+4,12));point=self.read(p)
        self.events.append((0,point,zone,above))
        cpu.reg_write(UC_X86_REG_EAX,(self.q['component']+int(self.profile==5 and (point^self.seed)&1))&0xffffffff)
    def lookup(self,cpu,address,size,user):
        raw=self.read(cpu.reg_read(UC_X86_REG_ESP)+0x2c);obj=self.read(cpu.reg_read(UC_X86_REG_ECX)+0x24)
        self.events.append((2,raw,obj,0))
    def metadata(self,point):
        x,y=point&65535,point>>16;index=signed16(y)*512+signed16(x)
        valid=0<=index<262144 and index%7!=self.seed%7
        h=((index if valid else 0xffffffff)^self.seed*0x9e3779b9)&0xffffffff
        c=dict(point=pack(index%512,index//512) if valid else point,level=self.seed%9-4,
               bridge=self.profile in (2,3,6) and bool(h&1),ground=(h>>4)&255 if self.profile==2 else 255 if self.profile==3 else 0,
               upper=(h>>12)&255 if self.profile==2 else 0,overlay=0 if self.profile==4 and h&4 else -1,
               present=True,flag=True,allowed=bool(self.seed&1),land=(h>>20)%12)
        return c,index if valid else None
    def run(self,seed):
        self.seed=seed;self.profile=seed//64%8;edge=(0,1,510,511,65535,32767,32768,65534)
        origin=pack(edge[seed%8],edge[seed//8%8]);w=0 if seed%29==0 else seed%5+1;h=-1 if seed%31==0 else seed//5%5+1
        self.q=dict(speed=seed%8,component=seed+400 if seed%3==0 else 0xffffffff,zone=seed//7%15-1,
                    level=seed%9-4+(4 if seed%3==0 else 0) if self.profile==6 else -1,above=seed&1,reject=seed%4==0)
        table=[0x3f800000]*108
        for land in range(12):
            if self.profile==1:table[land*9+1]=0;table[land*9+2]=0x3f000000
            if self.profile==3:
                for speed in range(8):table[land*9+speed]=0
            if self.profile==7:
                for speed in range(8):table[land*9+speed]=(0,0x80000000,0x3f800000,0x7fc00001,0xbf800000,0x7f800000)[(land+speed+seed)%6]
        fallback,_=self.metadata(0xffffffff);self.configure(fallback,table,0);self.write_cell(self.fallback,fallback)
        self.cpu.mem_write(self.VECTOR,bytes(262144*4));used={}
        for x in range(max(0,w)):
            for y in range(max(0,h)):
                point=pack((origin&65535)+x,(origin>>16)+y);c,index=self.metadata(point)
                if index is not None and index not in used:
                    address=self.POOL+len(used)*0x200;used[index]=address;self.write_cell(address,c);self.word(self.VECTOR+index*4,address)
        snapshots={address:bytes(self.cpu.mem_read(address,0x150)) for address in used.values()}
        fallback_before=bytes(self.cpu.mem_read(self.fallback,0x150))
        self.word(self.POINT,origin);q=self.q
        args=(self.STOP,self.POINT,w&0xffffffff,h&0xffffffff,q['speed'],q['component'],q['zone']&0xffffffff,q['level']&0xffffffff,q['above'],q['reject'])
        self.cpu.mem_write(self.STACK,struct.pack('<10I',*args));self.cpu.reg_write(UC_X86_REG_ESP,self.STACK)
        self.cpu.emu_start(0x550cf0 if self.base else 0x56e7c0,self.STOP,count=100000)
        assert self.cpu.reg_read(UC_X86_REG_EIP)==self.STOP and self.cpu.reg_read(UC_X86_REG_ESP)==self.STACK+40
        assert self.read(self.POINT)==origin,'rectangle changed input Cell'
        for address,before in snapshots.items():assert bytes(self.cpu.mem_read(address,0x150))==before,'rectangle changed Cell state'
        fallback_after=bytes(self.cpu.mem_read(self.fallback,0x150))
        assert fallback_after[:0x24]+fallback_after[0x28:]==fallback_before[:0x24]+fallback_before[0x28:],'rectangle changed fallback fields beyond the lookup coordinate'
        result=self.cpu.reg_read(UC_X86_REG_EAX)&255;assert result in (0,1)
        digest=2166136261
        for value in (result,*(v for event in self.events for v in event)):digest=((digest^value)*16777619)&0xffffffff
        return digest
if __name__=='__main__':
    oracle=RectangleOracle(sys.argv[1],'--base' in sys.argv);total=2166136261
    for seed in range(512):
        h=oracle.run(seed)
        if '--vectors' in sys.argv:print(seed,f'{h:08x}')
        total=((total^h)*16777619)&0xffffffff
    assert total==(0x711b80fe if oracle.base else 0xa18ead0d),'original rectangle fit vectors changed'
    print('512 original rectangle fit vectors',f'{total:08x}',file=sys.stderr)
