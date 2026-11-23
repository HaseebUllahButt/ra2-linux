#!/usr/bin/env python3
"""Development probe for the base Unit unlimbo chain.

This uses the simulation oracle's map and locomotor fixtures. It executes the
original Unit, Foot, Techno, Object and empty-cell entry code. It supplies map
records and unrecovered renderer and type callbacks. It is not a whole game.
"""
import pathlib
import os
import struct
import sys
from unicorn import UcError
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
import original_route_check as r
m=r.m; cpu=m.cpu
if not m.base: raise SystemExit('base game.exe and --base are required')
full_battle='--battle' in sys.argv
real_fixture='RA2_WORLD_MAP_FIXTURE' in os.environ
center_x,center_y=(135,80) if real_fixture else (54,69)
original_argv=sys.argv[:]
if full_battle:
    sys.argv=[sys.argv[0],sys.argv[1],'--base','42','55' if real_fixture else '64','0','0','0','--unit','--aux']
    import original_house_probe as fixture
    sys.argv=original_argv
    house,scenario=fixture.house,fixture.scenario
    # Copy the actual Unit table before adapting fixture services.
    cpu.mem_write(m.VT,bytes(cpu.mem_read(0x7addf8,0x600)))
    m.word(m.FOOT,m.VT)
    m.word(m.LOCO+4,r.interface)
else:
    r.setup((0,),15)
    cpu.mem_map(0x1400000,0x20000)
    house=0x1400000;unit_counts=0x1418000
    m.word(m.FOOT+0x1b4,house);m.word(house+0x5474,unit_counts);m.word(house+0x5478,512)
    cpu.mem_map(0x1500000,0x10000)
    scenario=0x1500000;m.word(0xa3d290,scenario)
    cpu.reg_write(UC_X86_REG_ECX,scenario+0x218);cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,42));cpu.emu_start(0x638790,0x103e000,count=100000)
    for _ in range(3):
        cpu.reg_write(UC_X86_REG_ECX,scenario+0x218);cpu.reg_write(UC_X86_REG_ESP,0x103f000)
        cpu.mem_write(0x103f000,struct.pack('<III',0x103e000,450,1800));cpu.emu_start(0x6388a0,0x103e000,count=100000)
    cpu.reg_write(UC_X86_REG_ECX,scenario+0x218);cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    cpu.mem_write(0x103f000,struct.pack('<I',0x103e000));cpu.emu_start(0x638840,0x103e000,count=100000)
map_rules=fixture.rules if full_battle else r.RULES
# Legacy comparisons supplied 104. This is Rules/LeptonsPerSightIncrease,
# not map LevelHeight; newer cases pass the separate original rule value.
sight_divisor=int(os.environ.get('RA2_SIGHT_DIVISOR','104'))
if sight_divisor<=0: raise SystemExit('sight divisor must be positive')
m.word(map_rules+0x1324,sight_divisor)
if not full_battle: m.word(m.FOOT+0x114,0)
size=100 if real_fixture else 80
m.word(0x8324e0+0xf0,size);m.word(0x8324e0+0xf4,size)
local=(5,5,90,89) if real_fixture else (2,2,76,72)
for offset,value in zip((0xf8,0xfc,0x100,0x104),local): m.word(0x8324e0+offset,value)
directions=((0,-1),(1,-1),(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1))
bias=int(sys.argv[3]) if len(sys.argv)>3 else 0
level=int(sys.argv[4]) if len(sys.argv)>4 and sys.argv[4]!='--battle' else 0
mark=int(sys.argv[5]) if len(sys.argv)>5 and sys.argv[5]!='--battle' else 0
for i,(dx,dy) in enumerate(directions): cpu.mem_write(m.cell_for(center_x+dx,center_y+dy)+0x122,bytes([(bias+i)&255]))
m.word(m.TYPE+0xb94,-1)
m.word(m.TYPE+0x5a4,1) # AMCV: Crusher=yes and no SpeedType; 0070ABEA selects Track.
if full_battle:
    assert cpu.mem_read(m.FOOT+0x76,1)==b'\x01' and cpu.mem_read(m.FOOT+0x74,1)==b'\x00'
    assert m.read(m.FOOT+0x80)==0xffffffff
    assert m.read(m.FOOT+0x98)==0xffffffff and m.read(m.FOOT+0xa0)==0xffffffff
else:
    cpu.mem_write(m.FOOT+0x76,b'\x01');cpu.mem_write(m.FOOT+0x74,b'\x00')
    m.word(m.FOOT+0x80,-1)
    m.word(m.FOOT+0x98,-1);m.word(m.FOOT+0xa0,-1)
cpu.mem_write(0xa40958,b'\x01');m.word(0xa40774,0)
m.word(m.VT+0x194,0x703040);m.word(m.VT+0x198,0x4c8730)
m.word(m.VT+0x11c,0x4c2e60)
m.word(m.VT+0x16c,0x594770)
m.word(m.VT+0x80,0x6c12c0);m.word(m.VT+0x84,0x705020);m.word(m.VT+0x100,0x5d4470)
m.word(m.VT+0x74,0x4ca130);m.virtual(0x2a0,0,m.ret(0));m.virtual(0x38,0,m.ret(0))
# Fresh Foot +5A0 is set by 004C2B6E. Use the original occupancy getter
# and Unit +E8 so linked placement updates the Cell +124 byte.
if not full_battle: cpu.mem_write(m.FOOT+0x5a0,b'\x01')
assert cpu.mem_read(m.FOOT+0x5a0,1)==b'\x01'
m.word(m.VT+0xbc,0x41b470);m.word(m.VT+0xe8,0x707980)
m.virtual(0x180,4,m.ret(0))
m.virtual(0x418,16,m.ret(0))
m.virtual(0x414,8,m.ret(0));m.virtual(0x1e8,0,m.ret(0))
m.virtual(0x54,0,m.ret(0))
m.word(m.VT+0x12c,0x5d36e0);m.bind(0x4e09a0,4,m.ret(0))
cpu.mem_write(m.TYPE+0x205,b'\x01')
setup_calls=[]
def note_cell_setup():
    setup_calls.append(m.cpu.reg_read(UC_X86_REG_ECX))
m.bind(0x472540,4,note_cell_setup)
m.bind(0x568140,4,m.ret(1))
m.bind(0x5348a0,8,m.ret(1))
m.bind(0x54a550,16,m.ret(0))
m.bind(0x476b20,8,m.ret(0))
m.word(m.FOOT+0x564,m.LOCO+4)
m.word(m.VT+0x2f4,0x4c8ca0)
raw=pathlib.Path(sys.argv[1]).read_bytes()
cpu.mem_write(0x55a740,raw[0x15a740:0x15a750])
m.callbacks.pop(0x55a740,None)
type_vtable,type_copy=0x1007000,0x1017000
m.word(m.TYPE,type_vtable);m.word(type_vtable+0x6c,type_copy)
m.word(type_vtable+0x90,0x5d5d90);m.word(type_vtable+0x40,0x1017010)
m.bind(0x1017010,0,m.ret(0));m.bind(0x78298a,0,m.ret(0))
def copy_position():
    target=m.argument(0);source=m.argument(1)
    cpu.mem_write(target,bytes(cpu.mem_read(source,12)))
    cpu.reg_write(UC_X86_REG_EAX,target)
m.bind(type_copy,8,copy_position)
point=0x100d000
for speed in (0.0,1.0):
    x=center_x+2 if speed==0 else center_x
    destination=m.cell_for(x,center_y)
    cpu.mem_write(destination+0x11b,struct.pack('<bB',level,0))
    cpu.mem_write(destination+0x116,struct.pack('<h',-1))
    m.word(destination+0x54,-1);m.word(destination+0x58,-1)
    m.word(destination+0x140,(0x40000 if mark else 0) | (0x200 if '--flag200' in sys.argv else 0))
    m.coord(point,(x*256+128,center_y*256+128,level*104))
    land=2 if speed==0 else 0
    m.word(destination+0xec,land)
    cpu.mem_write(0x850ea0+(land*9+1)*4,struct.pack('<f',speed))
    m.word(destination+0x4c,4 if speed==0 else 0)
    cpu.reg_write(UC_X86_REG_ECX,m.FOOT);cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    cpu.mem_write(0x103f000,struct.pack('<III',0x103e000,point,32))
    try: cpu.emu_start(0x6fc940,0x103e000,count=100000)
    except UcError:
        print('unresolved chain dependency',hex(cpu.reg_read(UC_X86_REG_EIP)),[hex(a) for a in m.recent]);raise
    assert cpu.reg_read(UC_X86_REG_EIP)==0x103e000, 'original Unit placement did not return'
    result=cpu.reg_read(UC_X86_REG_EAX)&0xff
    on_map=cpu.mem_read(m.FOOT+0x32d,1)[0]
    head=m.read(destination+0xe4)
    assert on_map==1
    unit_counts=m.read(house+0x5474)
    house_count=m.read(unit_counts) if unit_counts else 0
    assert (result,head,house_count)==((0,0,0) if speed==0 else (1,m.FOOT,1)),(speed,result,hex(head),house_count)
    assert bool(m.read(destination+0x140)&0x40000)==bool(mark)
    assert bool(m.read(destination+0x140)&0x200)==('--flag200' in sys.argv)
    hull=struct.unpack('<H',cpu.mem_read(m.FOOT+0x2e0,2))[0]>>8
    turret=struct.unpack('<H',cpu.mem_read(m.FOOT+0x2f8,2))[0]>>8
    neighbor=[cpu.mem_read(m.cell_for(center_x+dx,center_y+dy)+0x122,1)[0] for dx,dy in directions]
    state=bytes(cpu.mem_read(scenario+0x218,1012));indexes=struct.unpack_from('<II',state,4)
    table=struct.unpack_from('<250I',state,12)
    z=m.signed(m.FOOT+0x90) if result else -1
    height=cpu.mem_read(m.FOOT+0x378,1)[0] if result else -1
    occupancy=cpu.mem_read(destination+0x124,1)[0]
    assert len(setup_calls)==int(bool(result))
    if result: assert setup_calls[-1]==destination
    print('PLACE_RESULT',result,on_map,4 if head==m.FOOT else 0,occupancy,int(bool(setup_calls)),house_count,m.read(m.FOOT+0x30),
          hull,turret,z,height,*neighbor,*indexes,*table)
