#!/usr/bin/env python3
"""Compare the original Cell fit body, including its real land-table reads.

Usage: original_cell_fit_check.py executable [--base] [--vectors]
Only the component helper is supplied. Occupancy, heights, overlay gates,
selected speed columns, x87 comparisons, and AL returns execute unchanged.
"""
import struct
import sys
from original_image import load_original
from unicorn import UC_HOOK_CODE,UC_HOOK_MEM_READ
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_FPCW

def inputs(seed):
    q=dict(speed=1,low=False,a0=False,component=0xffffffff,zone=0,level=-1,above=False)
    c=dict(point=(seed*31&65535)|(seed*47&65535)<<16,level=0,bridge=False,ground=0,upper=0,overlay=-1,present=True,flag=False,allowed=False,land=0)
    table=[0x3f800000]*108;actual=0
    if seed<32:
        q['speed']=1+(seed&1);c['land']=0 if seed<16 else 5
        table[c['land']*9+1]=(0 if seed//2&1 else 0x3f800000) if seed<16 else 0x3f333333
        table[c['land']*9+2]=(0x3f800000 if seed//2&1 else 0) if seed<16 else 0x3f000000
    elif seed<2080:
        n=(seed-32)%1024;q['low']=bool(n&1);q['a0']=bool(n&2)
        if seed<1056:c['ground']=n//4
        else:c.update(bridge=True,ground=255,upper=n//4);table[1]=0x7fc00001 if seed&4 else 0
    elif seed<2208:
        n=seed-2080;h=(-128,-1,0,127)[n%4];c.update(level=h,bridge=bool(n&4));q.update(above=bool(n&8),level=(-1,h,h+4,h+7)[n//16%4])
    elif seed<2688:
        n=seed-2208;q['zone']=n%15-1;c.update(overlay=0,present=bool(n//15&1),flag=bool(n//30&1),allowed=bool(n//60&1),land=3);table[28]=0
    else:
        value=seed^0x612e374a
        def next_value():
            nonlocal value
            value=(value*1664525+1013904223)&0xffffffff;return value>>16
        q.update(speed=next_value()%8,low=bool(next_value()%2),a0=bool(next_value()%2))
        q['component']=0xffffffff if seed%3 else 0xffff if seed%5 else seed
        actual=(q['component']+int(seed%7==0))&0xffffffff
        q.update(zone=next_value()%15-1,above=bool(next_value()%2))
        c.update(level=next_value()%256-128,bridge=bool(next_value()%2));h=c['level'];q['level']=(-1,h,h+4,h+1)[next_value()%4]
        c['ground']=next_value()%256 if seed%5==0 else 0;c['upper']=next_value()%256 if seed%5==1 else 0
        c.update(overlay=0 if seed%3==0 else -1,present=bool(next_value()%2),flag=bool(next_value()%2),allowed=bool(next_value()%2),land=next_value()%12)
        table[c['land']*9+q['speed']]=(0,0x80000000,0x3f800000,0x3f000000,0xbf800000,0x7fc00001,0x7f800001,0x7f800000,0xff800000,1,0x80000001)[next_value()%11]
    return q,c,table,actual

class FitOracle:
    CELL,OVERLAY,REGISTRY,STACK,STOP=0x1000000,0x1001000,0x1002000,0x10ff000,0x10fe000
    def __init__(self,path,base):
        self.base=base;self.cpu=load_original(path);self.cpu.mem_map(0x1000000,0x200000)
        self.table=0x850ea0 if base else 0x89ea40;self.events=[];self.actual=0
        self.component_address=0x54f8f0 if base else 0x56d230
        self.word(0xa35dec if base else 0xa83d84,self.REGISTRY)
        self.cpu.mem_write(self.component_address,b'\xc2\x0c\x00')
        self.cpu.hook_add(UC_HOOK_CODE,self.component,begin=self.component_address,end=self.component_address)
        self.cpu.hook_add(UC_HOOK_MEM_READ,self.table_read,begin=self.table,end=self.table+431)
        for address in (0x638840,0x6388a0) if base else (0x65c780,0x65c7e0):
            self.cpu.hook_add(UC_HOOK_CODE,self.rng,begin=address,end=address)
        self.cpu.reg_write(UC_X86_REG_FPCW,0xe7f)
    def word(self,address,value):self.cpu.mem_write(address,struct.pack('<I',value&0xffffffff))
    def read(self,address):return struct.unpack('<I',self.cpu.mem_read(address,4))[0]
    def component(self,cpu,address,size,user):
        esp=cpu.reg_read(UC_X86_REG_ESP);point,zone,above=struct.unpack('<III',cpu.mem_read(esp+4,12))
        self.events.append((0,self.read(point),zone,above));cpu.reg_write(UC_X86_REG_EAX,self.actual)
    def table_read(self,cpu,access,address,size,value,user):
        assert size==4 and (address-self.table)%4==0
        index=(address-self.table)//4;self.events.append((1,index//9,index%9,self.read(address)))
    def rng(self,*args):raise AssertionError('Cell fit consumed shared RNG')
    def write_cell(self,address,c):
        self.word(address+0x24,c['point']);self.word(address+0x44,c['overlay']);self.word(address+0xec,c['land'])
        self.cpu.mem_write(address+0x11b,bytes([c['level']&255]))
        self.cpu.mem_write(address+0x124,bytes([c['ground']]))
        self.cpu.mem_write(address+0x128,bytes([c['upper']]))
        self.word(address+0x140,0x100 if c['bridge'] else 0)
    def configure(self,c,table,actual):
        self.events=[];self.actual=actual
        self.cpu.mem_write(self.table,struct.pack('<108I',*table))
        self.word(self.REGISTRY,self.OVERLAY if c['present'] else 0)
        self.cpu.mem_write(self.OVERLAY+(0x278 if self.base else 0x2a8),bytes([c['flag']]))
        self.cpu.mem_write(self.OVERLAY+(0x1fd if self.base else 0x22d),bytes([c['allowed']]))
    def run(self,seed):
        q,c,table,actual=inputs(seed);self.configure(c,table,actual);self.write_cell(self.CELL,c)
        before=bytes(self.cpu.mem_read(self.CELL,0x150));table_before=bytes(self.cpu.mem_read(self.table,432))
        overlay_before=bytes(self.cpu.mem_read(self.OVERLAY,0x300))
        args=(self.STOP,q['speed'],q['low'],q['a0'],q['component'],q['zone']&0xffffffff,q['level']&0xffffffff,q['above'])
        self.cpu.mem_write(self.STACK,struct.pack('<8I',*args))
        self.cpu.reg_write(UC_X86_REG_ESP,self.STACK);self.cpu.reg_write(UC_X86_REG_ECX,self.CELL)
        self.cpu.emu_start(0x478760 if self.base else 0x4834a0,self.STOP,count=10000)
        assert self.cpu.reg_read(UC_X86_REG_EIP)==self.STOP and self.cpu.reg_read(UC_X86_REG_ESP)==self.STACK+32
        assert bytes(self.cpu.mem_read(self.CELL,0x150))==before,'original Cell fit changed Cell state'
        assert bytes(self.cpu.mem_read(self.table,432))==table_before,'original Cell fit changed land data'
        assert bytes(self.cpu.mem_read(self.OVERLAY,0x300))==overlay_before,'original Cell fit changed OverlayType data'
        result=self.cpu.reg_read(UC_X86_REG_EAX)&255;assert result in (0,1)
        h=2166136261
        for value in (result,*(v for event in self.events for v in event)):h=((h^value)*16777619)&0xffffffff
        return h
if __name__=='__main__':
    oracle=FitOracle(sys.argv[1],'--base' in sys.argv);total=2166136261
    for seed in range(4096):
        h=oracle.run(seed)
        if '--vectors' in sys.argv:print(seed,f'{h:08x}')
        total=((total^h)*16777619)&0xffffffff
    assert total==(0xa04450e6 if oracle.base else 0xf80fc9b5),'original Cell fit vectors changed'
    print('4096 original Cell fit vectors',f'{total:08x}',file=sys.stderr)
