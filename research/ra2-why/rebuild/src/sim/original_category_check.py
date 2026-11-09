#!/usr/bin/env python3
"""Run the original Cell +4C producer with resolved ordered occupant records.
Only usable-area and constant runtime-type virtual answers are adapted.
"""
import struct
import sys
from original_image import load_original
from unicorn import UC_HOOK_CODE,UC_HOOK_MEM_WRITE,UcError
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
cpu=load_original(sys.argv[1]); base='--base' in sys.argv
cpu.mem_map(0x1000000,0x100000)
CELL,OVERLAY,TYPES,SESSION=0x1000000,0x1001000,0x1002000,0x1003000
ACTORS,BUILDING,HOUSE,TERRAIN,VT=0x1010000,0x1020000,0x1028000,0x1030000,0x1038000
STACK,STOP=0x10ff000,0x10fe000
usable=True; writes=[]
def word(address,value): cpu.mem_write(address,struct.pack('<I',value&0xffffffff))
def ret(value,pop):
    esp=cpu.reg_read(UC_X86_REG_ESP)
    cpu.reg_write(UC_X86_REG_EAX,value)
    cpu.reg_write(UC_X86_REG_EIP,struct.unpack('<I',cpu.mem_read(esp,4))[0])
    cpu.reg_write(UC_X86_REG_ESP,esp+4+pop)
def usable_answer(cpu,address,size,data): ret(int(usable),8)
def kind_answer(cpu,address,size,data):
    kind=struct.unpack('<I',cpu.mem_read(cpu.reg_read(UC_X86_REG_ECX)+0x34,4))[0]
    ret(kind,0)
def write_trace(cpu,access,address,size,value,data):
    if address==CELL+0x4c: writes.append(value)
cpu.hook_add(UC_HOOK_CODE,usable_answer,begin=0x55a660 if base else 0x578460,end=0x55a660 if base else 0x578460)
cpu.hook_add(UC_HOOK_CODE,kind_answer,begin=0x1039000,end=0x1039000)
cpu.hook_add(UC_HOOK_MEM_WRITE,write_trace,begin=CELL+0x4c,end=CELL+0x4f)
word(VT+0x2c,0x1039000); word(TYPES,OVERLAY)
word(0xa35dec if base else 0xa83d84,TYPES); word(0xa3d290 if base else 0xa8b230,SESSION)
# Inputs below are test records, not gameplay defaults.
def inputs(seed):
    state=0x51ed270b^seed
    def next():
        nonlocal state
        state=(state*1664525+1013904223)&0xffffffff
        return state>>16
    present=next()%2; flags=next()%16; land=next()%13
    floats=(0x00000000,0x80000000,0x3f800000,0x3f000000,0xbf800000,0x3c23d709,0x3c23d70a,0x3c23d70b,0x7fc00000,0x7f800000,0xff800000)
    overlay_wheel=floats[next()%len(floats)]; land_wheel=floats[next()%len(floats)]
    actors=[]
    for i in range(next()%5):
        kinds=(6,36,28,15); kind=kinds[next()%4]
        actors.append((kind,next()%8,(0,8,12,17)[next()%4],next()%9,next()%9))
    mode=next()%3; valid=next()%8!=0
    if seed==0: present,flags,land,land_wheel,actors,valid=0,0,0,0x3f800000,[],True
    if seed in (1,2,3,4,5,6):
        present,flags,land,land_wheel,actors,valid=0,0,0,0x3f800000,[(6,2,0,0,0)],True
        if seed==2: actors.append((6,2,0,0,0))
        if seed==3: actors.append((36,0,0,0,0))
        if seed==4: actors.append((36,0,0,7,7))
        if seed==5: actors=[(6,5,0,0,0)]
        if seed==6: actors=[(6,1,0,0,0)]
    return present,flags,land,overlay_wheel,land_wheel,actors,mode,valid

def run(seed):
    global usable,writes
    present,flags,land,overlay_wheel,land_wheel,actors,mode,usable=inputs(seed)
    word(CELL+0x44,0 if present else -1); word(CELL+0xec,land); word(CELL+0x4c,0xdeadbeef)
    fields=(0x1fd,0x278,0x285,0x284) if base else (0x22d,0x2a8,0x2b5,0x2b4)
    for bit,offset in enumerate(fields): cpu.mem_write(OVERLAY+offset,bytes([(flags>>bit)&1]))
    # Separate overlay and cell land slots retain both supplied Wheel values.
    word(OVERLAY+(0x268 if base else 0x298),13)
    table=0x850ea8 if base else 0x89ea48
    word(table+13*36,overlay_wheel); word(table+land*36,land_wheel)
    word(SESSION+(0x838 if base else 0x1258),mode)
    word(CELL+0xe4,ACTORS if actors else 0)
    for i,(kind,building_flags,building_state,terrain_a,terrain_b) in enumerate(actors):
        actor=ACTORS+i*0x1000; building=BUILDING+i*0x2000; house=HOUSE+i*0x1000; terrain=TERRAIN+i*0x1000
        word(actor,VT); word(actor+0x34,kind); word(actor+0x30,actor+0x1000 if i+1<len(actors) else 0)
        word(actor+(0x418 if base else 0x520),building)
        cpu.mem_write(building+(0x135a if base else 0x16c0),bytes([building_flags&1]))
        cpu.mem_write(building+(0x1359 if base else 0x16bf),bytes([(building_flags>>1)&1]))
        word(actor+(0x1b4 if base else 0x21c),house)
        cpu.mem_write(house+(0x142 if base else 0x1fa),bytes([(building_flags>>2)&1]))
        word(actor+(0x4ec if base else 0x618),building_state)
        word(actor+(0xb0 if base else 0xc8),terrain)
        word(terrain+(0x278 if base else 0x2a8),terrain_a); word(terrain+(0x27c if base else 0x2ac),terrain_b)
    writes=[]
    cpu.reg_write(UC_X86_REG_ECX,CELL); cpu.reg_write(UC_X86_REG_ESP,STACK); word(STACK,STOP)
    try: cpu.emu_start(0x478f40 if base else 0x483c80,STOP,count=10000)
    except UcError:
        print('unresolved original category dependency at',hex(cpu.reg_read(UC_X86_REG_EIP))); raise
    assert cpu.reg_read(UC_X86_REG_EIP)==STOP,'category instruction bound exceeded'
    result=struct.unpack('<I',cpu.mem_read(CELL+0x4c,4))[0]
    assert writes and result==writes[-1]
    value=2166136261
    for item in (result,len(writes),*writes): value=((value^item)*16777619)&0xffffffff
    return value,result,writes

if __name__=='__main__':
    count=0; value=2166136261
    for seed in range(4096):
        result,category,writes=run(seed)
        value=((value^result)*16777619)&0xffffffff; count+=1
        if '--vectors' in sys.argv: print('{%d,0x%08xu},'%(seed,result))
    if '--vectors' not in sys.argv: print('original coarse category cases=%d hash=%08X'%(count,value))
