#!/usr/bin/env python3
"""Run the original regular path search with a resolved flat cell-entry adapter.
This checks search behavior separately from UnitClass cell eligibility.
Usage: original_path_check.py executable [--base]
"""
import struct
import sys
import original_route_check as r
from unicorn import UcError
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
m=r.m; cpu=m.cpu; base=m.base
cpu.mem_map(0x2000000,0x1000000)
SEARCH=0x2000000
allocation=0x2010000

def allocate(size):
    global allocation
    address=allocation; allocation+=(size+4095)&~4095
    assert allocation<0x3000000,'oracle allocation bound exceeded'
    return address

def new(): cpu.reg_write(UC_X86_REG_EAX,allocate(m.argument()))
m.bind(0x782017 if base else 0x7c8e17,0,new)

def invoke(address,this,*args):
    cpu.reg_write(UC_X86_REG_ECX,this); cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    cpu.mem_write(0x103f000,struct.pack('<'+'I'*(len(args)+1),0x103e000,*[a&0xffffffff for a in args]))
    try: cpu.emu_start(address,0x103e000,count=10000000)
    except UcError:
        print('unresolved path dependency',hex(cpu.reg_read(UC_X86_REG_EIP)),[hex(a) for a in m.recent],hex(m.read(cpu.reg_read(UC_X86_REG_ESP))))
        raise
    assert cpu.reg_read(UC_X86_REG_EIP)==0x103e000,'oracle instruction bound exceeded'
    return cpu.reg_read(UC_X86_REG_EAX)

invoke(0x427fb0 if base else 0x42a6d0,SEARCH)
# Original map-size input uses width+height+1. This oracle uses a 512-wide search grid.
RECT=0x100d000
m.word(RECT+8,255); m.word(RECT+12,256)
invoke(0x4284e0 if base else 0x42ac00,SEARCH,RECT)
# One region contains all resolved flat test cells. Other cells are not allocated.
regions=allocate(512*512*10)
map_object=0x8324e0 if base else 0x87f7e8
shift=4 if base else 0
m.word(map_object+0xf4-shift,255); m.word(map_object+0xf8-shift,256); m.word(map_object+0x6c-shift,512*512)
m.word(map_object+0x70-shift,regions)
m.word(SEARCH+0x40,allocate(8)); m.word(SEARCH+0x28,1)
blocked=set()
last_cost=0
def capture_cost():
    global last_cost
    last_cost=m.read(m.argument()+4)
m.callbacks[0x428370 if base else 0x42aa90]=capture_cost
def can_enter():
    cell=m.argument(); pair=m.read(cell+0x24)
    point=(pair&65535,pair>>16)
    cpu.reg_write(UC_X86_REG_EAX,7 if point in blocked else 0)

# The C runtime exit-registration callback does not affect the search output.
m.bind(0x78298a if base else 0x7c978a,0,m.ret(0))

def run(goal,walls=()):
    global blocked,last_cost
    last_cost=0
    r.setup((0,),15); blocked=set(walls)
    m.virtual(0x194 if base else 0x1ac,20,can_enter)
    for y in range(65,74):
        for x in range(50,59):
            cell=m.cell_for(x,y)
            cpu.mem_write(cell+0x116,struct.pack('<h',-1))
            cpu.mem_write(regions+(y*512+x)*10,struct.pack('<h',1))
    invoke(0x427e90 if base else 0x42a5b0,SEARCH)
    start=0x100d020; end=0x100d024; output=0x100e000
    m.word(start,54|(69<<16)); m.word(end,goal[0]|(goal[1]<<16))
    result=invoke(0x427370 if base else 0x429a90,SEARCH,start,end,m.FOOT,output,-1,0)
    if not result: return None
    count=m.read(result+8)
    assert count<1000,count
    directions=tuple(m.signed(output+i*4) for i in range(count))
    return (count,directions,last_cost)

if __name__=='__main__':
    cases=[((x,y),0,()) for y in range(65,74) for x in range(50,59)]
    cases.extend((((54,67),1,((54,67),)),((54,67),2,tuple((x,68) for x in range(50,59))),((57,67),3,((55,68),))))
    for goal,scenario,walls in cases:
        result=run(goal,walls)
        if '--vectors' in sys.argv:
            count,directions,cost=result or (0,(),0)
            print('{%d,%d,%d,%d,0x%08xu,{%s}},'%(goal[0],goal[1],scenario,count,cost,','.join(map(str,directions))))
        else: print('original regular path',goal,scenario,result)
