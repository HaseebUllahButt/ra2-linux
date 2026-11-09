#!/usr/bin/env python3
"""Run original Overlay mark control with explicit resolved helper bodies.

Usage: original_overlay_mark_check.py executable [--base] [--vectors]
The original dispatcher executes its own cell writes. Parent mark, map lookup,
placement queries, bridge setters, allocation, setup and disposal are adapters.
Bridge approach geometry remains outside these vectors. Allocation returns null.
Animation creation therefore reaches the no-owner query without constructing an animation.
"""
import struct
import sys
from unicorn import UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP
import original_move_trace as m

cpu=m.cpu
base=m.base
cpu.mem_map(0x1200000,0x20000)
RULES,TABLE,OTHER=0x1200000,0x1204000,0x1208000
root=m.cell_for(20,20)
directions=((0,-1),(1,-1),(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1))
points=[root]+[m.cell_for(20+x,20+y) for x,y in directions]
point_index={address:i-1 for i,address in enumerate(points)}
type_pointer=0x98 if base else 0xac
index_offset=0x264 if base else 0x294
override_address=0xa40774 if base else 0xa8e7ac
import_address=0x7e8a8c if base else 0x8333bc
clock_address=0x833688 if base else 0x880994
limbo=0x76 if base else 0x81
events=[]
active=False
state={}
kinds=('ParentMark','LocateCell','BridgeA','BridgeB','VeinholeFits','CreateVeinhole',
       'CreateCrate','WallFits','UpdateWall','MarkZone','RebuildZones','Clearance',
       'UpdateResource','CreateAnimation','TileSetup','FinishObject','Dispose',
       'WriteOverlay','WriteData','WriteOwner50','IncrementNeighbor')
def event(kind,value=0,second=0,third=0,neighbor=-1):
    events.append((kinds.index(kind),value,second,third,neighbor))
def answer(value): cpu.reg_write(UC_X86_REG_EAX,int(value))
def query(kind,key):
    def call():
        event(kind,m.argument() if kind=='ParentMark' else 1 if kind=='WallFits' else 0)
        answer(state[key])
    return call
def simple(kind,count=0):
    def call():
        args=[m.argument(i) for i in range(count)]
        event(kind,*args)
        answer(0)
    return call
def locate(): event('LocateCell'); answer(0x1009000)
def neighbor():
    x,y=directions[m.argument()]
    answer(m.cell_for(20+x,20+y))
def clearance():
    assert [m.argument(i) for i in range(3,7)]==[0xffffffff,0,0xffffffff,1]
    event('Clearance',*[m.argument(i) for i in range(3)])
    if state['mutate']: m.word(override_address,1)
    answer(state['clear'])
def resource():
    event('UpdateResource',m.argument())
    if state['mutate']:
        address=m.TYPE+index_offset+0x16
        cpu.mem_write(address,bytes([not cpu.mem_read(address,1)[0]]))
    answer(0)
def allocate():
    size=m.argument()
    if size==(0xf0 if base else 0x108): event('CreateVeinhole')
    else:
        assert size==(0x1a8 if base else 0x1c8),'unexpected allocation branch'
        event('CreateAnimation')
    answer(0)

m.bind(0x5d4160 if base else 0x5f5850,4,query('ParentMark','parent'))
m.bind(0x4732d0 if base else 0x47e040,8,simple('BridgeA',2))
m.bind(0x473700 if base else 0x47e470,8,simple('BridgeB',2))
m.bind(0x710a60 if base else 0x74d670,0,query('VeinholeFits','vein_fits'))
m.bind(0x47ad00 if base else 0x485af0,0,simple('CreateCrate'))
m.bind(0x4718b0 if base else 0x47c620,12,query('WallFits','wall_fits'))
m.bind(0x475960 if base else 0x480630,4,simple('UpdateWall',1))
m.bind(0x54fc60 if base else 0x56d5a0,4,simple('MarkZone'))
m.bind(0x566340 if base else 0x584550,4,simple('RebuildZones'))
m.bind(0x476ac0 if base else 0x481810,4,neighbor)
m.bind(0x478760 if base else 0x4834a0,28,clearance)
m.bind(0x476ba0 if base else 0x4818e0,4,resource)
m.bind(0x472540 if base else 0x47d2b0,4,simple('TileSetup',1))
# operator new is cdecl. The fixture supplies failed allocation after the branch.
m.bind(0x782017 if base else 0x7c8e17,0,allocate)
m.bind(0x47a220 if base else 0x485010,0,m.ret(-1))

def writes(cpu,access,address,size,value,user):
    if not active: return
    if address==m.FOOT+0x74:
        assert size==1 and value==0
        event('FinishObject')
    for point in points:
        offset=address-point
        if offset==0x44: event('WriteOverlay',value,neighbor=point_index[point])
        if offset==0x11e: event('WriteData',value,neighbor=point_index[point])
        if offset==0x50: event('WriteOwner50',value,neighbor=point_index[point])
        if offset==0x122: event('IncrementNeighbor',1,neighbor=point_index[point])
cpu.hook_add(UC_HOOK_MEM_WRITE,writes)

def inputs(seed):
    value=seed^0x93c56a71
    def next_value():
        nonlocal value
        value=(value*1664525+1013904223)&0xffffffff
        return value>>16
    indexes=(24,25,237,238,167,126,178,102,0,1,121,127,232,239)
    index=indexes[next_value()%14]; mode=next_value()%5; ramp=next_value()%8
    land=(0,5,9)[next_value()%3]; wall=next_value()%4==0
    override=next_value()%3==0; slot=next_value()%3
    token=-1 if next_value()%2 else 17
    existing=-1 if next_value()%2 else 40
    parent=next_value()%4!=0; clear=next_value()%2; wall_fits=next_value()%2; vein_fits=next_value()%2
    blocks=next_value()%2; field=next_value()%2; mutate=next_value()%2
    if seed<256: mode=1 if seed%2 else 3; ramp=seed%5; parent=True
    return dict(index=index,mode=mode,ramp=ramp,land=land,wall=wall,override=override,
                slot=slot,token=token,existing=existing,parent=parent,clear=clear,
                wall_fits=wall_fits,vein_fits=vein_fits,blocks=blocks,field=field,mutate=mutate)

def run(seed):
    global state,events,active
    state=inputs(seed); events=[]; active=False; m.next_stub=0x1010000
    cpu.mem_write(m.FOOT,bytes(0x200)); cpu.mem_write(m.TYPE,bytes(0x400)); m.word(m.FOOT,m.VT)
    m.word(m.FOOT+type_pointer,m.TYPE); m.word(m.TYPE+index_offset,state['index'])
    m.word(m.TYPE+index_offset+4,state['land'])
    m.word(m.TYPE+index_offset+8,0x120d000 if seed%7==0 else 0)
    cpu.mem_write(m.TYPE+index_offset+0x14,bytes([state['wall']]))
    cpu.mem_write(m.TYPE+index_offset+0x16,bytes([state['field']]))
    m.word(override_address,state['override']); m.word(import_address,state['token']); m.word(clock_address,0x87654321)
    m.word(0x839848 if base else 0x8871e0,RULES)
    for slot in (0xf0,0xf4,0xf8) if base else (0xf8,0xfc,0x100): m.word(RULES+slot,OTHER)
    if state['slot']==1: m.word(RULES+(0xf8 if base else 0x100),m.TYPE)
    if state['slot']==2: m.word(RULES+(0xf4 if base else 0xfc),m.TYPE)
    m.word(0xa35dec if base else 0xa83d84,TABLE); m.word(TABLE+40*4,OTHER)
    cpu.mem_write(OTHER+(0x282 if base else 0x2b2),bytes([state['blocks']]))
    for i,point in enumerate(points):
        cpu.mem_write(point,bytes(0x200)); m.word(point+0x24,(20|(20<<16)))
        m.word(point+0x44,state['existing'] if i==0 else -1)
        cpu.mem_write(point+0x11e,bytes([(seed+i*29)&255]))
        cpu.mem_write(point+0x122,bytes([(seed+i*37)&255]))
        m.word(point+0x50,0x12340000+i)
    cpu.mem_write(root+0x11c,bytes([state['ramp']]))
    cpu.mem_write(m.FOOT+0x74,b'\x01'); m.word(0x1009000,20|(20<<16))
    m.virtual(0x1a0 if base else 0x1b8,4,locate)
    m.virtual(0xf0 if base else 0xf8,0,simple('Dispose'))
    m.word(0x103f000,0x103e000); m.word(0x103f004,state['mode'])
    cpu.reg_write(UC_X86_REG_ECX,m.FOOT); cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    active=True
    cpu.emu_start(0x5da970 if base else 0x5fc570,0x103e000,count=100000)
    active=False
    result=cpu.reg_read(UC_X86_REG_EAX)&255
    h=2166136261
    def mix(v):
        nonlocal h
        h=((h^(v&0xffffffff))*16777619)&0xffffffff
    mix(0 if result else 1)
    for row in events:
        for value in row: mix(value)
    for point in points:
        mix(m.read(point+0x44)); mix(cpu.mem_read(point+0x11e,1)[0])
        mix(m.read(point+0x50)); mix(cpu.mem_read(point+0x122,1)[0])
    mix(cpu.mem_read(m.FOOT+0x74,1)[0]); mix(cpu.mem_read(m.FOOT+limbo,1)[0])
    return h

total=2166136261
for seed in range(4096):
    digest=run(seed)
    if '--vectors' in sys.argv: print(seed,f'{digest:08x}')
    total=((total^digest)*16777619)&0xffffffff
print('4096 original overlay dispatch cases',f'{total:08x}',file=sys.stderr)
