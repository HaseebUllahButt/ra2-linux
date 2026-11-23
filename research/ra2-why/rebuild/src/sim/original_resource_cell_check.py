#!/usr/bin/env python3
"""Compare native resource setup with the owned base game.exe.

Usage: original_resource_cell_check.py /path/to/game.exe /path/to/native-test
The original 00476BA0, 005DC0B0, 00548070, 006388A0 and 00491620
bodies execute without stubs. Map, type, and RNG memory is supplied below.
"""
import struct
import subprocess
import sys
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP
from original_image import load_original

cpu=load_original(sys.argv[1])
cpu.mem_map(0x2000000,0x800000)
OTT,RES,RT,SCEN,CELL,NBR=0x2003000,0x2004000,0x2005000,0x2006000,0x2008000,0x2009000
OVER,MAPTABLE,STACK,STOP=0x2140000,0x2200000,0x27ff000,0x27fe000
RNG=SCEN+0x218
MAP,FALLBACK=0x8324e0,0xa6f9f8
DIRS=((0,-1),(1,-1),(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1))
CHOICES=(-1,5,10,11,12,13,20,21,22,23,30)

def put(address,value): cpu.mem_write(address,struct.pack('<I',value&0xffffffff))
def get(address): return struct.unpack('<I',cpu.mem_read(address,4))[0]
def s32(value): return value if value<0x80000000 else value-0x100000000
def call(address,this,*args):
    cpu.reg_write(UC_X86_REG_ECX,this); cpu.reg_write(UC_X86_REG_ESP,STACK)
    cpu.mem_write(STACK,b''.join(struct.pack('<I',x&0xffffffff) for x in (STOP,)+args))
    cpu.emu_start(address,STOP,count=200000)
    assert cpu.reg_read(UC_X86_REG_EIP)==STOP,hex(cpu.reg_read(UC_X86_REG_EIP))
    return s32(cpu.reg_read(UC_X86_REG_EAX))

put(0xa3d290,SCEN); put(0xa35dec,OTT); put(0xac0b34,RT); put(0xac0b40,2)
put(MAP+0x138,MAPTABLE)
for index in range(31):
    address=OVER+index*0x288
    put(OTT+index*4,address); put(address+0x264,index)
    cpu.mem_write(address+0x279,bytes([index!=5]))
call(0x491620,0)
assert struct.unpack('<16h',cpu.mem_read(0x851a70,32))==tuple(v for pair in DIRS for v in pair)
active=False; writes=[]; lookups=[]
def write_hook(cpu,access,address,size,value,user):
    if not active: return
    if address==CELL+0x44: writes.append(('overlay',value&0xffffffff))
    if address==CELL+0x11e: writes.append(('data',value&255))
    if address==FALLBACK+0x24: writes.append(('fallback',value&0xffffffff))
def lookup_hook(cpu,address,size,user):
    if active:
        ptr=get(cpu.reg_read(UC_X86_REG_ESP)+4)
        lookups.append(get(ptr))
cpu.hook_add(UC_HOOK_MEM_WRITE,write_hook)
cpu.hook_add(UC_HOOK_CODE,lookup_hook,begin=0x548070,end=0x548070)

def inputs(seed):
    state=seed^0x9028ac17
    def nxt():
        nonlocal state
        state=(state*1664525+1013904223)&0xffffffff
        return state>>16
    def choice(): return CHOICES[nxt()%11]
    root=choice(); data=nxt()&255; fallback=choice()
    absent=(seed//5)%8 if seed%5==0 else -1
    neighbors=[choice() for _ in range(8)]
    if absent>=0: neighbors[absent]=fallback
    divisor0=1+nxt()%13; divisor1=1+nxt()%13
    table=[]
    for i in range(250):
        hi=nxt(); lo=nxt(); table.append((hi<<16)|lo)
    return root,data,fallback,absent,neighbors,divisor0,divisor1,table

def run(seed,native):
    global active,writes,lookups
    root,data,fallback,absent,neighbors,d0,d1,table=inputs(seed)
    first=seed%250; second=(seed*7+13)%250; disabled=(seed%5==1)
    rng_before=bytes([disabled,0xa5,0x56,0xca])+struct.pack('<II250I',first,second,*table)
    cpu.mem_write(RNG,rng_before)
    put(CELL+0x24,20|(20<<16)); put(CELL+0x44,root); cpu.mem_write(CELL+0x11e,bytes([data]))
    put(FALLBACK+0x24,0x22221111); put(FALLBACK+0x44,fallback)
    for i,(dx,dy) in enumerate(DIRS):
        address=NBR+i*0x200
        put(address+0x44,neighbors[i])
        offset=((20+dy)*512+20+dx)*4
        put(MAPTABLE+offset,0 if i==absent else address)
    for i,(start,divisor,value) in enumerate(((10,d0,0x70000001 if seed%9==0 else 25),
                                              (12 if seed%3==0 else 20,d1,-50))):
        typ=RES+i*0x200; put(RT+i*4,typ)
        put(typ+0x98,1-i if seed%7==0 else i)
        put(typ+0xb8,value); put(typ+0xe0,OVER+start*0x288)
        put(typ+0xe4,divisor); put(typ+0xe8,3); put(typ+0xec,1)
    writes=[]; lookups=[]; active=True
    result=call(0x476ba0,CELL,seed%4!=0)
    active=False
    values=iter(native)
    assert next(values)==seed
    actual_overlay=s32(get(CELL+0x44)); actual_data=cpu.mem_read(CELL+0x11e,1)[0]
    assert [actual_overlay,actual_data,result]==[next(values),next(values),next(values)],(seed,'result')
    overlay_written=any(k=='overlay' for k,v in writes)
    data_written=any(k=='data' for k,v in writes)
    assert [int(overlay_written),int(data_written),get(FALLBACK+0x24)]==[next(values),next(values),next(values)],(seed,'writes')
    count=next(values); native_fallback=[(next(values),next(values)) for _ in range(count)]
    expected_lookups=[((20+dx)&65535)|(((20+dy)&65535)<<16) for dx,dy in DIRS]
    if data_written:
        assert lookups==expected_lookups,(seed,lookups)
    else: assert not lookups,(seed,lookups)
    observed_fallback=[(i,expected_lookups[i]) for i in range(8) if i==absent and data_written]
    assert native_fallback==observed_fallback,(seed,'fallback order')
    assert [v for k,v in writes if k=='fallback']==[v for i,v in observed_fallback],(seed,'fallback writes')
    native_random=[next(values),next(values)]+list(values)
    assert len(native_random)==252
    actual_random=list(struct.unpack('<II250I',cpu.mem_read(RNG+4,1008)))
    assert native_random==actual_random,(seed,'random state')
    assert bytes(cpu.mem_read(RNG,4))==rng_before[:4],(seed,'random header')

vectors=subprocess.check_output([sys.argv[2],'--vectors'],text=True).splitlines()
assert len(vectors)==2048
for line in vectors:
    native=list(map(int,line.split())); run(native[0],native)
print('2048 original resource setups match native results, Cell writes, neighbor order, and full random state')
