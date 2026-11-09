#!/usr/bin/env python3
"""Execute the original fixed Foot nearest-cell search with read-only answers.

Usage: original_nearest_cell_check.py executable [--base] [--edges] [--vectors]
The scan, height gate, ordered candidate groups, frame selection and original
distance helper run unchanged. Usable-area, rectangle and projection answers
are resolved fixtures. No runtime Windows code is used by the native game.
"""
import struct
import sys
from original_image import load_original
from unicorn import UC_HOOK_CODE,UcError
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_FPCW
cpu=load_original(sys.argv[1]); base='--base' in sys.argv
cpu.mem_map(0x1000000,0x900000)
TABLE,GOAL,DATA,OUT=0x1100000,0x1200000,0x1201000,0x1202000
STACK,STOP=0x18ff000,0x18fe000
MAP=0x8324e0 if base else 0x87f7e8
FALLBACK=0xa6f9f8 if base else 0xabdc50
EMPTY=0xa6f238 if base else 0xabd480
state={}; events=[]; callbacks={}
def word(address,value): cpu.mem_write(address,struct.pack('<I',value&0xffffffff))
def read(address): return struct.unpack('<I',cpu.mem_read(address,4))[0]
def argument(index=0): return read(cpu.reg_read(UC_X86_REG_ESP)+4+4*index)
def unpack(cell): return struct.unpack('<hh',struct.pack('<I',cell))
def pack(x,y): return (x&0xffff)|((y&0xffff)<<16)
def hashed(cell):
    x,y=unpack(cell); return (x*17+y*31+state['seed']*13)&0xffffffff
def metadata(cell):
    h=hashed(cell);x,y=unpack(cell);special=state['profile'] in (3,6);level=state['seed']%7-3
    if special: level=0
    elif cell!=state['goal']: level+=(h>>3)%5-2
    usable=0<=x<512 and 0<=y<512 and (special or state['profile']==4 or h%11!=0)
    return usable,level,False if special else bool((h>>8)&1)
def fit(cell):
    if state['profile']==4:return False
    if state['profile']==3:return True
    x,y=unpack(cell);gx,gy=unpack(state['goal'])
    if state['profile']==6:return y==gy and abs(x-gx)==1
    if state['profile'] in (0,2) and cell==state['goal']:return False
    return (hashed(cell)>>8)%7==0
def write_metadata(address,cell):
    usable,level,bridge=metadata(cell)
    cpu.mem_write(address+0x11b,bytes([level&255]));word(address+0x140,0x100 if bridge else 0)
    return usable
def bind(address,pop,callback):
    callbacks[address]=callback;cpu.mem_write(address,b'\xc2'+struct.pack('<H',pop))
    cpu.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
def hook(cpu,address,size,user):
    if address in callbacks:callbacks[address]()
cpu.reg_write(UC_X86_REG_FPCW,0xe7f)
def usable():
    address=argument();cell=read(address+0x24);assert argument(1)==1
    answer=write_metadata(address,cell);events.append((0,cell,int(answer),0));cpu.reg_write(UC_X86_REG_EAX,int(answer))
def rectangle():
    args=[argument(i) for i in range(9)];cell=read(args[0])
    component=0xffffffff if state['component']==0xffff else state['component']
    assert args[1:3]==[1,1] and args[3]==state['speed'] and args[4]==component and args[5]==state['zone']
    assert args[6]==0xffffffff and args[7]&255==state['above'] and args[8]&255==0
    answer=fit(cell);events.append((1,cell,int(answer),component));events.append((2,args[3],args[5],args[7]&255))
    cpu.reg_write(UC_X86_REG_EAX,int(answer))
def projection():
    x,y,z=struct.unpack('<iii',cpu.mem_read(argument(1),12));assert z==0
    cell=pack((x-128)//256,(y-128)//256)
    if state['profile'] in (3,5) or (state['profile']!=6 and hashed(cell)%4==0):
        cx,cy=unpack(cell);cell=pack(cx+1,cy)
    events.append((3,x&0xffffffff,y&0xffffffff,cell));word(argument(),cell);cpu.reg_write(UC_X86_REG_EAX,argument())
bind(0x55a740 if base else 0x578540,8,usable)
bind(0x550cf0 if base else 0x56e7c0,36,rectangle)
bind(0x6a6a50 if base else 0x6d6410,8,projection)
def rng():raise AssertionError('nearest-cell search consumed shared RNG')
callbacks[0x638840 if base else 0x65c780]=rng;callbacks[0x6388a0 if base else 0x65c7e0]=rng
for address in (0x638840,0x6388a0) if base else (0x65c780,0x65c7e0):
    cpu.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
word(0x832618 if base else 0x87f924,TABLE)
# Execute the real registered static initializer, after poisoning both sentinel words.
initializer=0x5441a0 if base else 0x5618b0
assert read(0x7ca814 if base else 0x813a98)==initializer, 'nearest sentinel initializer registration differs'
word(EMPTY,0xa5a55a5a);word(STACK,STOP);cpu.reg_write(UC_X86_REG_ESP,STACK)
cpu.emu_start(initializer,STOP,count=1000)
assert read(EMPTY)==0, 'original nearest sentinel initializer differs'
def inputs(seed):
    profile=seed%8;gx,gy=50+seed%9,65+(seed//9)%9;empty=0
    if '--edges' in sys.argv:
        edge=(0,1,510,511);gx,gy=edge[seed%4],edge[(seed//4)%4]
    source=empty if seed%5==0 or profile==7 else pack(gx+seed%7-3,gy+seed%11-5)
    if profile==6:source=pack(gx,gy)
    width,height=1+seed%16,1+(seed//16)%16
    if profile==4:width,height=20,20
    if seed%23==0:width,height=0,0
    return dict(seed=seed,profile=profile,goal=pack(gx,gy),source=source,empty=empty,width=width,height=height,
                speed=(seed//3)%8,zone=seed%12,component=0xffff if seed%11==0 else 0xffffffff if seed%13==0 else seed+100,
                above=int(seed%2==0),frame=1000+seed*77)
def run(seed):
    global state,events
    state=inputs(seed);events=[]
    cpu.mem_write(TABLE,bytes(512*512*4));cpu.mem_write(GOAL,bytes(0x200));cpu.mem_write(FALLBACK,bytes(0x148))
    x,y=unpack(state['goal']);word(TABLE+(y*512+x)*4,GOAL);word(GOAL+0x24,state['goal']);write_metadata(GOAL,state['goal'])
    word(MAP+(0xf0 if base else 0xf4),state['width']);word(MAP+(0xf4 if base else 0xf8),state['height'])
    word(EMPTY,state['empty']);word(0xa40d2c if base else 0xa8ed84,state['frame'])
    word(DATA,state['goal']);word(DATA+4,state['source'])
    args=[STOP,OUT,DATA,state['speed'],state['component'],state['zone'],state['above'],1,1,0,1,0,1,DATA+4,0]
    if not base:args.append(0)
    cpu.mem_write(STACK,struct.pack('<'+'I'*len(args),*args));cpu.reg_write(UC_X86_REG_ECX,MAP);cpu.reg_write(UC_X86_REG_ESP,STACK)
    try:cpu.emu_start(0x5502e0 if base else 0x56dc20,STOP,count=10000000)
    except UcError:
        print('unresolved nearest dependency',hex(cpu.reg_read(UC_X86_REG_EIP)),file=sys.stderr);raise
    assert cpu.reg_read(UC_X86_REG_EIP)==STOP,'original nearest bound exceeded'
    value=2166136261
    for word_value in (read(OUT),*(word for event in events for word in event)):
        value=((value^word_value)*16777619)&0xffffffff
    return value
if __name__=='__main__':
    total=2166136261
    for seed in range(512):
        digest=run(seed)
        if '--vectors' in sys.argv:print(seed,f'{digest:08x}')
        total=((total^digest)*16777619)&0xffffffff
    assert total==(0x4e70df25 if '--edges' in sys.argv else 0xa7f1ecff), 'original nearest-cell vectors changed'
    print('512 original nearest-cell scans',f'{total:08x}',file=sys.stderr)
