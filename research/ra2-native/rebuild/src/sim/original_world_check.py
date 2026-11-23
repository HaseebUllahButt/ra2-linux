#!/usr/bin/env python3
"""Check ordered original non-overlay tile setup across both scenario passes.
Requires Unicorn. Usage: original_world_check.py executable [--base]
Tile classes contain resolved static test metadata. Passability effects are excluded.
The original Cell body, tile decisions, getters, and map iterator execute unchanged.
"""
import struct
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_FPCW
from original_image import load_original
base="--base" in sys.argv
cpu=load_original(sys.argv[1]); cpu.mem_map(0x1000000,0x400000)
CELL,TYPE,HEADERS,RECORDS,GRID=0x1000000,0x1100000,0x12a0000,0x12c0000,0x1300000
VT,TABLE,RULES,STOP,STACK=0x1070000,0x1080000,0x1060000,0x10fe000,0x10ff000
MAP=0x8324e0 if base else 0x87f7e8

def word(address,value): cpu.mem_write(address,struct.pack('<I',value&0xffffffff))
def read(address): return struct.unpack('<I',cpu.mem_read(address,4))[0]
def call(address,this=0,args=()):
    cpu.mem_write(STACK,struct.pack('<'+'I'*(len(args)+1),STOP,*[a&0xffffffff for a in args]))
    cpu.reg_write(UC_X86_REG_ESP,STACK); cpu.reg_write(UC_X86_REG_ECX,this)
    cpu.emu_start(address,STOP,count=100000)
    return cpu.reg_read(UC_X86_REG_EAX)
cpu.reg_write(UC_X86_REG_FPCW,0xe7f)
call(0x491620 if base else 0x49f2f0)
word(VT+0x9c,0x10fd000); cpu.mem_write(0x10fd000,b'\x8b\x81\x10\x03\x00\x00\xc3')
for i in range(2048):
    obj=TYPE+i*0x320; header=HEADERS+i*0x40; record=RECORDS+i*0x40
    word(TABLE+i*4,obj); word(obj,VT); word(obj+0x310,header); word(obj+(0x298 if base else 0x2c8),-1)
    word(header,1); word(header+4,1); word(header+8,60); word(header+12,30); word(header+16,record if i!=504 else 0)
    raw=12 if i==600 or 1000<=i<=1015 or 1100<=i<=1113 else 14 if i==300 else 0
    ramp=i-99 if 100<=i<=119 else (i-200)//3+1 if 200<=i<=211 else 0
    cpu.mem_write(record+0x29,bytes([raw,ramp]))
word(0xa40cd4 if base else 0xa8ed2c,TABLE); word(0xa40ce0 if base else 0xa8ed38,2048)
word(0x839848 if base else 0x8871e0,RULES); cpu.mem_write(RULES+(0x584 if base else 0x664),b'\x02')
word(0x832544 if base else 0x87f850,0x12f0000); word(0x83254c if base else 0x87f858,0x12f1000)
word(MAP+(0xf0 if base else 0xf4),3); word(MAP+(0xf4 if base else 0xf8),2); word(MAP+(0x68 if base else 0x6c),36); word(MAP+(0x138 if base else 0x13c),GRID)
word((0xa6f9f8 if base else 0xabdc50)+0x38,0xffff)
# This callback refreshes unsupported movement/passability cache fields only.
cpu.mem_write(0x478f40 if base else 0x483c80,b'\xc3')
globals=(0xabc1d8,0xaa1058,0xabc2b8,0xabb104,0xaa0e18,0xabc2b0,0xaa1134,0xaa0e24,0xaa0748,0xaa10a8,0xaa10a4,0xabad28,0xabbec8,0xaa0e20,0xaa1090)
base_globals=(0xa6e018,0xa52ea8,0xa6e0e8,0xa6cf4c,0xa52c60,0xa6e0e0,0xa52f84,0xa52c70,0xa52590,0xa52ef8,0xa52ef4,0xa6cb74,0xa6dd0c,0xa52c68,0xa52ee0)
for i,address in enumerate(base_globals if base else globals): word(address,(i+1)*100)
coords=((1,3),(2,2),(3,1),(2,3),(3,2),(2,4),(3,3),(4,2),(3,4),(4,3))
for n,(x,y) in enumerate(coords): word(GRID+(y*512+x)*4,CELL+n*0x200)
fixtures=(
    ('ramps',[100]*10,[0]*10,[0]*10),
    ('mixed',[100,101,102,103,300,600,700,900,1100,0xffff],[0]*10,[0,0,0,0,0,0,4,0,0,0]),
    ('invalid',[600,4096,504,0xffff,600,600,600,600,600,600],[2,5,0,7,0,0,0,0,0,0],[0]*10),
)
expected_hashes={"ramps":(0x51b86d71,0x3ed322b2),"mixed":(0x3f6d0fc3,0x22342bbc),"invalid":(0xac56b3d0,0xac56b3d0)}
for name,indexes,subtiles,levels in fixtures:
    for n,(x,y) in enumerate(coords):
        c=CELL+n*0x200; cpu.mem_write(c,b'\0'*0x200)
        word(c+0x140,0x100 if n%2 else 0)
        word(c+0x24,x|(y<<16)); word(c+0x38,indexes[n]); word(c+0x44,-1)
        cpu.mem_write(c+0x116,b'\xff\xff'); cpu.mem_write(c+0x11a,bytes([subtiles[n],levels[n],0]))
    for phase in range(2):
        call(0x55a550 if base else 0x578350,MAP)
        for n in range(10):
            c=call(0x55a490 if base else 0x578290,MAP); assert c==CELL+n*0x200
            call(0x472540 if base else 0x47d2b0,c,(-1,))
        assert not call(0x55a490 if base else 0x578290,MAP)
        h=2166136261
        for n in range(10):
            c=CELL+n*0x200
            assert bool(read(c+0x140)&0x100)==bool(n%2),'original tile pass changed bridge bit'
            values=(read(c+0x38),cpu.mem_read(c+0x11c,1)[0],read(c+0xec),cpu.mem_read(c+0x11a,1)[0])
            if '--trace' in sys.argv: print(name,phase+1,n,*values)
            for v in values: h=((h^v)*16777619)&0xffffffff
        assert h==expected_hashes[name][phase]
        print(name,phase+1,f'{h:08x}')
