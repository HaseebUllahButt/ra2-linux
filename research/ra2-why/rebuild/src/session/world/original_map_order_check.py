#!/usr/bin/env python3
"""Check the original base scenario map-load prefix in x86.

The original 0065F6BA..0065F73A instructions run. Subsystem functions and a
single-cell map iterator are supplied. Earlier map allocation and resize,
subsystem internals, and later Battle phases are outside this check.
"""
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_ESP
if len(sys.argv)!=2:
    raise SystemExit('usage: original_map_order_check.py game.exe')
sys.argv=[sys.argv[0],sys.argv[1],'--base']
import original_move_trace as m
events=[]
def event(name):
    def record(): events.append(name)
    return record
for address,pop,name in ((0x49cfd0,4,'tiles'),(0x6ef5c0,0,'tubes'),
                         (0x5db670,0,'overlay'),(0x472540,4,'cell-pass'),
                         (0x6e4260,0,'terrain')):
    m.bind(address,pop,event(name))
for address,pop in ((0x481fb0,0),(0x45ebd0,0),(0x4e09a0,4),(0x672160,4),
                    (0x55a550,0),(0x5dc180,0)):
    m.bind(address,pop,m.ret(0))
count=[0]
def next_cell():
    count[0]+=1
    m.cpu.reg_write(UC_X86_REG_EAX,m.cell_for(20,20) if count[0]==1 else 0)
m.bind(0x55a490,0,next_cell)
m.cpu.reg_write(UC_X86_REG_ESI,0)
m.cpu.reg_write(UC_X86_REG_EDI,0x100e000)
m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
m.cpu.emu_start(0x65f6ba,0x65f73a,count=100000)
assert events==['tiles','tubes','overlay','cell-pass','terrain'],events
print('base x86 loads tiles, tubes, overlays, the first Cell pass, and Terrain in order')

# Run the packed-overlay scan at 005DB780 across a row boundary. The decoder,
# overlay constructor, type registry and map membership are supplied. This
# checks the original scan's byte-to-cell arguments, not full map allocation.
records=[]
count=[0]
values={0:24,511:25,512:237}
def next_byte():
    index=count[0]
    count[0]+=1
    if index==514:
        m.cpu.emu_stop()
        return
    m.word(m.argument(0),values.get(index,-1))
def construct():
    position=m.read(m.argument(1))
    records.append((type_ids[m.argument(0)],position&65535,position>>16))
    m.cpu.reg_write(UC_X86_REG_EAX,m.FOOT)
m.bind(0x535380,8,next_byte)
m.bind(0x54aab0,4,m.ret(1))
m.bind(0x782017,0,m.ret(m.FOOT))
m.bind(0x5da7d0,12,construct)
m.virtual(0x9c,0,m.ret(1))
vector=0x100a000
m.word(0xa35dec,vector)
m.word(0xa3d298,0)
type_ids={}
for index in values.values():
    address=0x1003000+index*0x40
    m.word(address,m.VT)
    m.word(vector+index*4,address)
    type_ids[address]=index
m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
m.cpu.emu_start(0x5db780,0x103e000,count=1000000)
assert records==[(24,0,0),(25,511,0),(237,0,1)],records
print('base x86 scans packed Overlay bytes in row order and supplies their cell coordinates')
