#!/usr/bin/env python3
"""Compare base resource Overlay Cell setup with supplied inputs.

Usage: original_cell_setup_check.py executable [--base] [--vectors]
Tile images, resource lookup, Overlay fields, usable-area and Unit kind are supplied.
Tube, animation and shadow guards exclude effects. Any RNG call fails the check.
"""
import struct
import sys
from unicorn import UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
from original_image import load_original
cpu=load_original(sys.argv[1]); base=True
cpu.mem_map(0x1000000,0x700000)
CELL,TYPES,HEADERS,RECORDS=0x1000000,0x1010000,0x1020000,0x1030000
TABLE,RULES,ACTOR,VT,AVT=0x1040000,0x1041000,0x1044000,0x1045000,0x1046000
COARSE,HIERARCHY,STACK,STOP=0x1100000,0x1200000,0x16ff000,0x16fe000
MAP=0x8324e0 if base else 0x87f7e8
FALLBACK=CELL+0x1200
callbacks={}; writes=[]; active=False; state={}
offsets=((0,-1),(1,0),(0,1),(-1,0),(2,2),(1,1),(-1,1),(1,-1))
points={(20,20):CELL}
for i,(dx,dy) in enumerate(offsets): points[(20+dx,20+dy)]=CELL+(i+1)*0x200
def word(address,value): cpu.mem_write(address,struct.pack('<I',value&0xffffffff))
def read(address): return struct.unpack('<I',cpu.mem_read(address,4))[0]
def argument(): return read(cpu.reg_read(UC_X86_REG_ESP)+4)
def bind(address,pop,callback):
    callbacks[address]=callback; cpu.mem_write(address,b'\xc2'+struct.pack('<H',pop) if pop else b'\xc3')
def hook(cpu,address,size,user):
    if address in callbacks: callbacks[address]()
def lookup():
    point=struct.unpack('<hh',cpu.mem_read(argument(),4))
    cpu.reg_write(UC_X86_REG_EAX,points.get(point,FALLBACK))
def trace(cpu,access,address,size,value,user):
    if active and address==CELL+0x4c: writes.append(value)
def rng(): raise AssertionError('checked Cell setup consumed RNG')
cpu.hook_add(UC_HOOK_CODE,hook)
cpu.hook_add(UC_HOOK_MEM_WRITE,trace)
bind(0x548070 if base else 0x5657a0,4,lookup)
bind(0x55a660 if base else 0x578460,8,lambda:cpu.reg_write(UC_X86_REG_EAX,int(state['usable'])))
bind(0x1047000,0,lambda:cpu.reg_write(UC_X86_REG_EAX,read(cpu.reg_read(UC_X86_REG_ECX)+0x310)))
bind(0x1047010,0,lambda:cpu.reg_write(UC_X86_REG_EAX,1))
bind(0x78298a if base else 0x7c978a,0,lambda:cpu.reg_write(UC_X86_REG_EAX,0)) # Exit-registration service only.
callbacks[0x638840 if base else 0x65c780]=rng
callbacks[0x6388a0 if base else 0x65c7e0]=rng
word(VT+0x9c,0x1047000); word(AVT+0x2c,0x1047010)
# Execute the original eight-direction initializer before replacement reads it.
word(STACK,STOP); cpu.reg_write(UC_X86_REG_ESP,STACK)
cpu.emu_start(0x491620 if base else 0x49f2f0,STOP,count=100000)
raw=(0,1,2,3,4,6,7,8,9,10,11,12,13,14,15,0)
for i in range(80):
    obj=TYPES+i*0x320; header=HEADERS+i*0x40; record=RECORDS+i*0x40
    word(TABLE+i*4,obj); word(obj,VT); word(obj+0x310,header)
    word(obj+(0x264 if base else 0x294),i)
    cpu.mem_write(obj+(0x2b1 if base else 0x2e1),bytes([i%4==0]))
    for offset,value in ((0,1),(4,1),(8,60),(12,30+(i%5)*15),(16,record)): word(header+offset,value)
    word(record+4,i%13-40); word(record+0x18,i%11+10)
    cpu.mem_write(record+0x24,bytes([i%2])); cpu.mem_write(record+0x29,bytes([raw[i%16],0]))
word(0xa40cd4 if base else 0xa8ed2c,TABLE); word(0xa40ce0 if base else 0xa8ed38,80)
word(0x839848 if base else 0x8871e0,RULES)
OVERLAY,OVERLAY_VECTOR=0x1048000,0x1049000
word(0xa35dec,OVERLAY_VECTOR); word(OVERLAY_VECTOR+102*4,OVERLAY)
word(OVERLAY+0x268,5)
for offset in (0x1fd,0x278,0x27c,0x284,0x285): cpu.mem_write(OVERLAY+offset,b'\0')
bind(0x5dc0b0,0,lambda:cpu.reg_write(UC_X86_REG_EAX,0))
word(MAP+(0x64 if base else 0x68),COARSE); word(MAP+(0x68 if base else 0x6c),512*512)
word(MAP+(0x6c if base else 0x70),HIERARCHY)
word(MAP+(0xf0 if base else 0xf4),16); word(MAP+(0xf4 if base else 0xf8),495)
groups=(0xa6e018,0xa52ea8,0xa6e0e8,0xa6cf4c,0xa52c60,0xa6e0e0,0xa52f84,0xa52c70,0xa52590,0xa52ef8,0xa52ef4,0xa6cb74,0xa6dd0c,0xa52c68,0xa52ee0) if base else (0xabc1d8,0xaa1058,0xabc2b8,0xabb104,0xaa0e18,0xabc2b0,0xaa1134,0xaa0e24,0xaa0748,0xaa10a8,0xaa10a4,0xabad28,0xabbec8,0xaa0e20,0xaa1090)
for i,address in enumerate(groups): word(address,32 if i==2 else 48 if i==6 else -1)
# Resolved shadow ranges exclude every fixture receiver, so the real getter returns null.
for i in range(5): word((0xa52e74 if base else 0xaa102c)+i*4,1000)

def inputs(seed):
    value=seed^0xc9185a37
    def next_value():
        nonlocal value
        value=(value*1664525+1013904223)&0xffffffff
        return value>>16
    tile=32+next_value()%32; level=next_value()%17-8; cliff=next_value()%3
    field=next_value()&255; usable=next_value()%8!=0; next_value(); unit=True; subtile=0
    if seed%8==0: tile=0xffff
    if seed%8==1: tile=96
    if seed%8==2: tile=32; subtile=3
    neighbors=[]
    for _ in offsets: neighbors.append(((32,49,7,0xffff)[next_value()%4],level+next_value()%7-2))
    return dict(tile=tile,level=level,cliff=cliff,field=field,usable=usable,unit=unit,subtile=subtile,neighbors=neighbors)

def run(seed):
    global state,writes,active
    state=inputs(seed); writes=[]; active=False
    for i,(coord,address) in enumerate(points.items()):
        cpu.mem_write(address,bytes(0x200)); word(address+0x24,coord[0]|(coord[1]<<16)); word(address+0x44,-1)
        tile,level=(state['tile'],state['level']) if i==0 else state['neighbors'][i-1]
        word(address+0x38,tile); cpu.mem_write(address+0x11b,bytes([level&255]))
    cpu.mem_write(FALLBACK,bytes(0x200)); word(FALLBACK+0x38,0xffff); word(FALLBACK+0x44,-1)
    word(CELL+0x44,102)
    flags=(seed*2654435761)&0xffffffff
    for i in range(80):
        obj=TYPES+i*0x320
        word(obj+(0x298 if base else 0x2c8),7 if flags&0x20000 or i%3==0 else -1)
        word(obj+(0x2a4 if base else 0x2d4),0 if flags&0x20000 else 1)
    word(CELL+0x140,flags); cpu.mem_write(CELL+0x11a,bytes([state['subtile']]))
    cpu.mem_write(CELL+0x11d,bytes([state['field']]))
    cpu.mem_write(ACTOR,bytes(0x400)); word(ACTOR,AVT)
    cpu.mem_write(ACTOR+0x88,struct.pack('<iii',11111,22222,33333))
    actor_before=bytes(cpu.mem_read(ACTOR,0x400))
    word(CELL+0xe4,ACTOR if state['unit'] else 0)
    # Original Unit +E8 sets ground occupancy bit 20 before this setup call.
    # Preserve links, raw owner fields, Tube reference, neighbor count, and both occupancy words.
    word(CELL+0x54,-1); word(CELL+0x58,-1); word(CELL+0x116,-1)
    cpu.mem_write(CELL+0x122,bytes([seed&255])); word(CELL+0x124,32); word(CELL+0x128,0)
    kept=((0x54,4),(0x58,4),(0x116,4),(0x122,1),(0x124,4),(0x128,4),(0xe4,4),(0xe8,4))
    kept_before=[bytes(cpu.mem_read(CELL+offset,size)) for offset,size in kept]
    cpu.mem_write(RULES+(0x584 if base else 0x664),bytes([state['cliff']]))
    table=0x850ea8 if base else 0x89ea48
    for land in range(16): word(table+land*36,0x3f800000 if (seed+land)%3 else 0)
    word(table+5*36,0x3f000000)
    index=20*512+20
    cpu.mem_write(COARSE+index*4,bytes(4)); cpu.mem_write(HIERARCHY+index*10,bytes(10))
    cpu.mem_write(STACK,struct.pack('<II',STOP,0xffffffff))
    cpu.reg_write(UC_X86_REG_ECX,CELL); cpu.reg_write(UC_X86_REG_ESP,STACK)
    active=True
    cpu.emu_start(0x472540 if base else 0x47d2b0,STOP,count=100000)
    active=False
    assert cpu.reg_read(UC_X86_REG_EIP)==STOP,'original Cell setup bound exceeded'
    assert read(CELL+0x140)==flags,'original checked setup changed flags'
    assert actor_before==bytes(cpu.mem_read(ACTOR,0x400)),'original checked setup changed the placed Unit'
    assert kept_before==[bytes(cpu.mem_read(CELL+offset,size)) for offset,size in kept], 'original setup changed linked Unit occupancy or preserved Cell fields'
    if '--trace' in sys.argv and seed in (3,4,5,6,7):
        print('trace',seed,'input',state['tile'],'output',read(CELL+0x38),cpu.mem_read(CELL+0x11a,1)[0],cpu.mem_read(CELL+0x11c,1)[0],read(CELL+0xec),cpu.mem_read(CELL+0x11d,1)[0],bool(flags&0x100),bool(flags&0x20000),read(CELL+0x4c),cpu.mem_read(COARSE+index*4+1,1)[0])
    h=2166136261
    def mix(v):
        nonlocal h
        h=((h^(v&0xffffffff))*16777619)&0xffffffff
    mix(read(CELL+0x38)); mix(cpu.mem_read(CELL+0x11a,1)[0]); mix(cpu.mem_read(CELL+0x11c,1)[0]); mix(read(CELL+0xec))
    mix(cpu.mem_read(CELL+0x11d,1)[0]); mix(bool(read(CELL+0x140)&0x100)); mix(bool(read(CELL+0x140)&0x20000))
    mix(read(CELL+0x4c)); mix(len(writes))
    for value in writes: mix(value)
    assert cpu.mem_read(COARSE+index*4,1)[0]==read(CELL+0x4c)
    mix(cpu.mem_read(COARSE+index*4+1,1)[0]); mix(cpu.mem_read(HIERARCHY+index*10+8,1)[0])
    return h

total=2166136261
seeds=[seed for seed in range(512) if seed%8>=3]
for seed in seeds:
    digest=run(seed)
    if '--vectors' in sys.argv: print(seed,f'{digest:08x}')
    total=((total^digest)*16777619)&0xffffffff
print(len(seeds),'original checked single-Cell setups',f'{total:08x}',file=sys.stderr)
