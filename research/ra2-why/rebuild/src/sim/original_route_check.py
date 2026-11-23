#!/usr/bin/env python3
"""Check original connected flat drive paths with explicit map and Foot adapters.
Usage: original_route_check.py executable [--base] [--trace] [--vectors]
The wrapper, path preparation, curve following, and facing writes execute original x86.
This harness supplies a resolved direction queue. It does not replace the original path search.
"""
import pathlib
import struct
import sys
import original_move_trace as m
from unicorn import UcError
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP
cpu=m.cpu
base=m.base
wrapper=0x4a05f0 if base else 0x4b0500
raw=pathlib.Path(sys.argv[1]).read_bytes()
# Find the actual drive interface table from its wrapper slot.
hits=[i+0x400000-0x40 for i in range(0x390000 if base else 0x3e1000,0x3c9000 if base else 0x412000,4)
      if struct.unpack_from('<I',raw,i)[0]==wrapper]
assert len(hits)==1,hits
interface=hits[0]
crate=0x476920 if base else 0x481a00
facing_set=0x4b8b10 if base else 0x4c9300
# Restore helpers patched by the adjacent-segment harness. Execute their real instructions.
for address in (crate,facing_set):
    cpu.mem_write(address,raw[address-0x400000:address-0x400000+16])
    del m.callbacks[address]
path_offset=0x4d0 if base else 0x5e0
facing_offset=0x2e0 if base else 0x388
clock=0xa40d2c if base else 0xa8ed84
GOAL,GOAL_VT,RULES=0x100a000,0x100b000,0x100c000
DIRECTIONS=((0,-1),(1,-1),(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1))

# Execute original static direction initializers. Their output is used by preparation.
for initializer in ((0x491620,0x4916d0) if base else (0x49f2f0,0x49f3a0)):
    cpu.reg_write(UC_X86_REG_ESP,0x103f000); m.word(0x103f000,0x103e000)
    cpu.emu_start(initializer,0x103e000,count=10000)
# The map's direct cell array is needed by the original occupancy helper.
cpu.mem_map(0x1100000,0x100000)
m.word(0x832618 if base else 0x87f924,0x1100000)
old_cell_for=m.cell_for
def allocated_cell(x,y):
    address=old_cell_for(x,y)
    m.word(0x1100000+(y*512+x)*4,address)
    return address
m.cell_for=allocated_cell

def coordinates():
    out=m.argument()
    cpu.mem_write(out,bytes(cpu.mem_read(m.FOOT+m.position,12)))
    cpu.reg_write(UC_X86_REG_EAX,out)

def goal_coordinates():
    out=m.argument()
    cpu.mem_write(out,bytes(cpu.mem_read(GOAL+0x40,12)))
    cpu.reg_write(UC_X86_REG_EAX,out)

def goal_virtual(slot,pop,callback):
    address=m.next_stub; m.next_stub+=16
    m.word(GOAL_VT+slot,address); m.bind(address,pop,callback)

def setup(directions,speed):
    m.next_stub=0x1010000
    m.cells.clear()
    cpu.mem_write(0x1100000,bytes(0x100000))
    m.setup(directions[0]*32,speed)
    m.word(0x8529d0 if base else 0x8a07d0,104) # Original LevelHeight; floor oracle checks this value.
    m.word(m.LOCO+4,interface)
    m.word(m.LOCO+0x58,-1); cpu.mem_write(m.LOCO+0x63,b'\0')
    source=tuple(m.signed(m.FOOT+m.position+4*i) for i in range(3))
    goal=(source[0]+sum(DIRECTIONS[d][0] for d in directions)*256,
          source[1]+sum(DIRECTIONS[d][1] for d in directions)*256,0)
    m.coord(m.LOCO+0x34,goal); m.coord(m.LOCO+0x40,(0,0,0))
    m.word(m.FOOT+(0x494 if base else 0x5a4),GOAL)
    m.word(GOAL,GOAL_VT); m.coord(GOAL+0x40,goal)
    m.word(GOAL+0x24,(goal[0]//256)|((goal[1]//256)<<16))
    for i in range(24): m.word(m.FOOT+path_offset+i*4,directions[i] if i<len(directions) else -1)
    # FacingClass current and desired angles agree. No initial turn is requested.
    m.word(m.FOOT+facing_offset,directions[0]<<13)
    m.word(m.FOOT+facing_offset+4,directions[0]<<13)
    m.word(m.FOOT+facing_offset+8,-1)
    m.word(0x839848 if base else 0x8871e0,RULES)
    # Full health is above ConditionYellow. The exact threshold is an original rules input.
    cpu.mem_write(RULES+(0x1368 if base else 0x1700),struct.pack('<d',0.5))
    m.word(0x850ea0 if base else 0x89ea40,0x3f800000)
    m.virtual(0x1a4 if base else 0x1bc,0,lambda: cpu.reg_write(UC_X86_REG_EAX,m.cell_for(m.signed(m.FOOT+m.position)//256,m.signed(m.FOOT+m.position+4)//256)))
    m.virtual(0x84 if base else 0x88,0,m.ret(m.TYPE))
    for slot in ((0x1bc,0x1c0,0x324,0x328) if base else (0x1d4,0x1d8,0x37c,0x380)): m.virtual(slot,0,m.ret(0))
    m.virtual(0x27c if base else 0x29c,0,m.ret(1))
    m.virtual(0x2ac if base else 0x2cc,0,m.ret(1))
    m.virtual(0x194 if base else 0x1ac,20,m.ret(0))
    m.virtual(0x4b8 if base else 0x534,8,m.ret(0))
    m.virtual(0xe8 if base else 0xf0,4,m.ret(0))
    m.virtual(0xec if base else 0xf4,4,m.ret(0))
    m.virtual(0x16c if base else 0x184,0,m.ret(2))
    m.virtual(0x410 if base else 0x480,8,m.ret(0))
    m.virtual(0x414 if base else 0x484,8,m.ret(0))
    m.virtual(0x1b8 if base else 0x1d0,0,m.ret(0))
    m.virtual(0x4c,8,coordinates)
    goal_virtual(0x2c,0,m.ret(11)); goal_virtual(0x4c,8,goal_coordinates)
    return goal

def run(directions,speed):
    goal=setup(directions,speed)
    h=2166136261
    for frame in range(1,16385):
        m.word(clock,frame)
        cpu.reg_write(UC_X86_REG_ESP,0x103f000)
        cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,m.LOCO+4))
        try: cpu.emu_start(wrapper,0x103e000,count=100000)
        except UcError:
            print('unresolved dependency at',hex(cpu.reg_read(UC_X86_REG_EIP)),[hex(a) for a in m.recent],hex(m.read(cpu.reg_read(UC_X86_REG_ESP))))
            raise
        assert cpu.reg_read(UC_X86_REG_EIP)==0x103e000,'instruction bound exceeded'
        current=tuple(m.signed(m.FOOT+m.position+4*i) for i in range(3))
        face=struct.unpack('<H',cpu.mem_read(m.FOOT+facing_offset+4,2))[0]>>8
        state=(*current,m.signed(m.LOCO+0x58),m.signed(m.LOCO+0x5c),m.signed(m.LOCO+0x4c),face,
               *[m.signed(m.FOOT+path_offset+4*i) for i in range(24)])
        if '--trace' in sys.argv: print(frame,*state)
        for value in state: h=((h^(value&0xffffffff))*16777619)&0xffffffff
        if m.signed(m.LOCO+0x58)==-1 and m.signed(m.FOOT+path_offset)==-1:
            assert current==goal,(directions,speed,current,goal)
            return frame,h
    raise AssertionError('original route did not complete')

def unresolved_search():
    raise AssertionError(('unexpected path search',tuple(m.signed(m.FOOT+m.position+4*i) for i in range(3)),tuple(m.signed(m.LOCO+0x34+4*i) for i in range(3)),[m.signed(m.FOOT+path_offset+4*i) for i in range(4)],m.recent))
m.bind(0x4c3000 if base else 0x4d3920,12,unresolved_search)

if __name__=='__main__':
    paths=[]
    for first in range(8):
        paths.extend((first,(first+delta)%8) for delta in (0,1,2,6,7))
        paths.extend((first,)*count for count in (3,23))
        paths.extend((first,first,(first+delta)%8) for delta in (1,2,6,7))
    for directions in paths:
        for speed in (1,7,15,30,255):
            frames,h=run(directions,speed)
            if '--vectors' in sys.argv:
                print('{{%s},%d,%d,%d,0x%08xu},'%(','.join(map(str,directions)),len(directions),speed,frames,h))
            else: print('original route',directions,speed,frames,'%08X'%h)
