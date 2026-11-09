#!/usr/bin/env python3
"""Run ordinary Unit branches of the original Foot path wrapper.

Usage: original_foot_path_check.py executable [--base] [--ordinary|--asymmetric|--distant] [--trace] [--vectors]
Queue guard, goal entry, marking, map lookup and path results are resolved adapters.
The wrapper, stack probe, coordinate getters, square root, conversion, metadata
no-op, queue copy, raw field writes and successful return execute unchanged.
"""
import struct
import sys
from unicorn import UC_HOOK_CODE,UcError
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_FPCW
from original_image import load_original
cpu=load_original(sys.argv[1]); base='--base' in sys.argv
cpu.mem_map(0x1000000,0x100000)
ACTOR,TYPE,VT,CELL,RULES,PATH=0x1000000,0x1003000,0x1005000,0x1008000,0x100a000,0x100c000
STACK,STOP=0x10ff000,0x10fe000
HOUSE=0x100e000
failure_mode=0xa3d298 if base else 0xa8b238
house_a,house_b=(0x134,0x135) if base else (0x1ec,0x1ed)
invalid_cell=0x865f00 if base else 0x8b3d88
original_vt=0x7addf8 if base else 0x7f5c70
position=0x88 if base else 0x9c
queue=0x4d0 if base else 0x5e0
raw=0x530 if base else 0x640
last_cell=0x450 if base else 0x558
clock=0xa40d2c if base else 0xa8ed84
state={}; events=[]; callbacks={}; distances=[]; recent=[]; axes=[0,0,0]
def word(address,value): cpu.mem_write(address,struct.pack('<I',value&0xffffffff))
def read(address): return struct.unpack('<I',cpu.mem_read(address,4))[0]
def argument(n=0): return read(cpu.reg_read(UC_X86_REG_ESP)+4+4*n)
def signed(value): return value if value<0x80000000 else value-0x100000000
def bind(address,pop,callback):
    callbacks[address]=callback
    cpu.mem_write(address,b'\xc2'+struct.pack('<H',pop) if pop else b'\xc3')
def hook(cpu,address,size,user):
    recent.append(address)
    if len(recent)>12: recent.pop(0)
    if address in callbacks: callbacks[address]()
cpu.hook_add(UC_HOOK_CODE,hook)
cpu.reg_write(UC_X86_REG_FPCW,0xe7f)
def guard():
    point=struct.unpack('<iii',cpu.mem_read(argument(),12)); events.append((0,*point))
    cpu.reg_write(UC_X86_REG_EAX,int(state['guard']))
def lookup():
    word(CELL+0x24,read(argument())); cpu.reg_write(UC_X86_REG_EAX,CELL)
def entry():
    assert [argument(i) for i in range(1,5)]==[0xffffffff,0xffffffff,0,1]
    events.append((1,read(argument()+0x24),0,0)); cpu.reg_write(UC_X86_REG_EAX,state['entry'])
def mark():
    events.append((2,argument(),0,0)); cpu.reg_write(UC_X86_REG_EAX,0)
def find():
    assert argument(2)==2000 and argument(3)==0
    assert argument(4)==state['append'] and argument(5)==state['mode']
    events.append((3,read(argument()),argument(4),argument(5)))
    if not state['present']:
        cpu.reg_write(UC_X86_REG_EAX,0); return
    words=state['path']
    cpu.mem_write(argument(1),struct.pack('<'+'i'*len(words),*words))
    for i,value in enumerate((0,state['cost'],len(words),argument(1),0,0,0,0)): word(PATH+i*4,value)
    cpu.reg_write(UC_X86_REG_EAX,PATH)
bind(0x1007000,4,guard); bind(0x1007010,20,entry); bind(0x1007020,4,mark)
bind(0x548070 if base else 0x5657a0,4,lookup)
bind(0x4bb350 if base else 0x4cbba0,24,find)
def computed_distance():
    distance=cpu.reg_read(UC_X86_REG_EAX)
    distances.append(distance)
    word(RULES+(0x1380 if base else 0x1718),distance+state['close_delta'])
callbacks[0x4c3110 if base else 0x4d3a30]=computed_distance
def stop():
    events.append((4,0,0,0))
    if state['distant'] and state['seed']%11==0:
        point=(state['goal'][0]*256+128+301,state['goal'][1]*256+128-87,state['source'][2])
        cpu.mem_write(ACTOR+position,struct.pack('<iii',*point)); state['after_position']=point
    cpu.reg_write(UC_X86_REG_EAX,0)
bind(0x1007030,4,stop)
def destination():
    point=read(argument()+0x24) if argument() else None
    events.append((5,int(point is not None),point or 0,argument(1)))
    state['destination']=point
    if point is None and state['seed']%7==0: cpu.mem_write(HOUSE+house_a,b'\x01')
    cpu.reg_write(UC_X86_REG_EAX,0)
def secondary():
    assert argument()==0
    events.append((6,0,0,0)); state['secondary']=True
    if state['seed']%4==0: cpu.mem_write(HOUSE+house_b,b'\x00')
    cpu.reg_write(UC_X86_REG_EAX,0)
def mission():
    assert argument(1)==0
    number=argument(); events.append((7,number,0,0)); state['mission']=number
    if number==11: word(failure_mode,(state['seed']//2)%2)
    cpu.reg_write(UC_X86_REG_EAX,0)
def failure_goal():
    assert argument(1)==ACTOR
    events.append((8,0,0,0)); word(argument(),state['fallback_goal'])
    cpu.reg_write(UC_X86_REG_EAX,argument())
bind(0x1007040,8,destination); bind(0x1007050,4,secondary); bind(0x1007060,8,mission)
bind(0x4eb270 if base else 0x500200,8,failure_goal)
# Observe the actual Cell Building traversal without changing its instructions.
callbacks[0x4717b0 if base else 0x47c520]=lambda:events.append((9,read(CELL+0x24),0,0))
def rng(): raise AssertionError('resolved ordinary Foot wrapper consumed RNG')
callbacks[0x638840 if base else 0x65c780]=rng
callbacks[0x6388a0 if base else 0x65c7e0]=rng

def inputs(seed):
    value=seed^0x5197ac63
    def next_value():
        nonlocal value
        value=(value*1664525+1013904223)&0xffffffff
        return value>>16
    goal=(50+next_value()%9,65+next_value()%9)
    source=((48+next_value()%16)*256+128+next_value()%97,
            (64+next_value()%16)*256+128-next_value()%97,(seed%5)*104)
    ordinary='--ordinary' in sys.argv
    asymmetric='--asymmetric' in sys.argv
    distant='--distant' in sys.argv
    present=not asymmetric and not distant and (ordinary or seed%9!=0)
    if not present:
        source=((goal[0]+seed%3-1)*256+128,(goal[1]+(seed//3)%3-1)*256+128,source[2])
    if distant:
        source=((goal[0]+2+seed%3)*256+128+seed%13-6,
                (goal[1]+(seed//3)%5-2)*256+128+seed%17-8,source[2])
        if seed%5==0:
            source=((goal[0]+seed%3-1)*256+128+seed%13-6,
                    (goal[1]+(seed//3)%3-1)*256+128+seed%17-8,source[2])
    append=next_value()%25; mode=next_value()%3; guard=next_value()%5!=0
    cost=next_value()%99+1; count=next_value()%48
    if seed%11==0: cost=0
    path=[next_value()%8 for _ in range(count)]+[-1]
    old=[(seed+i)%8 for i in range(24)]
    if seed%7==0: old[0]=-1
    sentinel=0xffffffff if seed%2 else 0x7fff7fff
    return dict(goal=goal,source=source,append=append,mode=mode,guard=guard,cost=cost,path=path,old=old,
                present=present,entry=7 if distant and seed%4==0 else 0 if ordinary else 6 if seed%4==0 else seed%6,close_delta=seed%3,
                delay=(.01,0.,.333,1./900,1.23456789,-.01,2.)[seed%7],above_bridge=False if distant and seed%5==0 else seed%2==0,
                bridge_goal=distant and seed%5==0,
                seed=seed,distant=distant,invalid=sentinel,
                fallback_goal=sentinel if seed%3==0 else (50+seed%7)|((64+seed%7)<<16),
                destination=None,secondary=False,mission=None)

def run(seed):
    global state,events,distances
    state=inputs(seed); events=[]; distances=[]
    cpu.mem_write(ACTOR,bytes(0x1000)); cpu.mem_write(TYPE,bytes(0x1000)); cpu.mem_write(VT,bytes(0x600))
    word(ACTOR,VT); word(ACTOR+(0x5ac if base else 0x6c4),TYPE)
    for slot in (0x48,0x2c,0x80 if base else 0x84,0x84 if base else 0x88,0x1a0 if base else 0x1b8,0x4c4 if base else 0x540,0x48c if base else 0x500):
        word(VT+slot,read(original_vt+slot))
    word(ACTOR+(0x564 if base else 0x674),TYPE+0x1100)
    word(TYPE+0x1100,VT+0x700); word(VT+0x748,0x1007030)
    cpu.mem_write(ACTOR+(0x79 if base else 0x8c),bytes([state['above_bridge']]))
    word(CELL+0x140,0x100 if state['bridge_goal'] else 0) # Explicit fixture flag; no terrain inference.
    word(VT+(0x2ac if base else 0x2cc),0x1007000)
    word(VT+(0x194 if base else 0x1ac),0x1007010)
    word(VT+(0x11c if base else 0x124),0x1007020)
    word(VT+(0x410 if base else 0x480),0x1007040)
    word(VT+(0x370 if base else 0x3c8),0x1007050)
    word(VT+(0x1d0 if base else 0x1e8),0x1007060)
    word(ACTOR+(0x1b4 if base else 0x21c),HOUSE)
    cpu.mem_write(HOUSE,bytes(0x300))
    cpu.mem_write(HOUSE+house_a,bytes([2 if seed%5==0 else 0]))
    cpu.mem_write(HOUSE+house_b,bytes([255 if seed%3==0 else 0]))
    word(failure_mode,int(seed%2==0)); word(invalid_cell,state['invalid'])
    cpu.mem_write(0xa40958 if base else 0xa8e9a0,bytes([seed%3!=0]))
    word(CELL+0xe4,0xdead0000 if seed%3==0 else 0 if seed%3==1 else ACTOR)
    cpu.mem_write(ACTOR+position,struct.pack('<iii',*state['source']))
    cpu.mem_write(ACTOR+queue,struct.pack('<24i',*state['old']))
    for i,value in enumerate((0x12345678,0x3456789a,0x56789abc)): word(ACTOR+raw+i*4,value)
    word(ACTOR+last_cell,0xabcdef01); word(clock,1000+seed)
    word(0x839848 if base else 0x8871e0,RULES); word(RULES+(0x1380 if base else 0x1718),576)
    cpu.mem_write(RULES+(0x13c8 if base else 0x1760),struct.pack('<d',state['delay']))
    goal=state['goal'][0]|(state['goal'][1]<<16)
    cpu.mem_write(STACK,struct.pack('<4I',STOP,goal,state['append'],state['mode']))
    cpu.reg_write(UC_X86_REG_ECX,ACTOR); cpu.reg_write(UC_X86_REG_ESP,STACK)
    try: cpu.emu_start(0x4c3000 if base else 0x4d3920,STOP,count=1000000)
    except UcError:
        print('unresolved Foot dependency',hex(cpu.reg_read(UC_X86_REG_EIP)),[hex(address) for address in recent],events,file=sys.stderr)
        raise
    assert cpu.reg_read(UC_X86_REG_EIP)==STOP,'ordinary Foot wrapper bound exceeded'
    after_position=tuple(struct.unpack('<iii',cpu.mem_read(ACTOR+position,12)))
    assert after_position==state.get('after_position',state['source'])
    fields=tuple(signed(read(ACTOR+raw+i*4)) for i in range(3))
    if '--asymmetric' in sys.argv and state['guard']:
        dx=state['source'][0]-(state['goal'][0]*256+128)
        dy=state['source'][1]-(state['goal'][1]*256+128)
        assert fields[1]==dy, 'original null Foot path did not retain Y delta'
        axes[0]+=dx==0 and dy!=0; axes[1]+=dy==0 and dx!=0; axes[2]+=dx*dy<0
    if '--trace' in sys.argv:
        print(seed,state['source'],state['goal'],'sqrt',distances,'fields',fields,'deltaY',state['source'][1]-(state['goal'][1]*256+128),events)
    h=2166136261
    def mix(value):
        nonlocal h
        h=((h^(value&0xffffffff))*16777619)&0xffffffff
    mix(cpu.reg_read(UC_X86_REG_EAX)&255)
    for word_value in struct.unpack('<24i',cpu.mem_read(ACTOR+queue,96)): mix(word_value)
    for field in fields: mix(field)
    mix(read(ACTOR+last_cell))
    mix(distances[0] if distances else -1)
    for event in events:
        for value in event: mix(value)
    if state['distant']:
        mix(0xffffffff if state['destination'] is None else state['destination'])
        mix(state['secondary']); mix(0xffffffff if state['mission'] is None else state['mission'])
        mix(read(failure_mode)); mix(cpu.mem_read(HOUSE+house_a,1)[0]); mix(cpu.mem_read(HOUSE+house_b,1)[0])
        for value in after_position: mix(value)
    return h

if __name__=='__main__':
    total=2166136261
    seeds=range(8) if '--trace' in sys.argv else range(512)
    for seed in seeds:
        digest=run(seed)
        if '--vectors' in sys.argv: print(seed,f'{digest:08x}')
        total=((total^digest)*16777619)&0xffffffff
    print(len(seeds),'original ordinary Foot path cases',f'{total:08x}',file=sys.stderr)
    if '--asymmetric' in sys.argv and '--trace' not in sys.argv:
        assert all(axes), 'original vectors missed zero axes or opposite signs'
        print('zero-X, zero-Y, opposite-sign cases:',*axes,file=sys.stderr)
