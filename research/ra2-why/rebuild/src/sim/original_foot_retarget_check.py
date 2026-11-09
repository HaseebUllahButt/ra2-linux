#!/usr/bin/env python3
"""Run bounded original Foot retarget decisions with resolved search helpers.

Usage: original_foot_retarget_check.py executable [--base] [--vectors]
The original Foot wrapper, static movement-zone table, Cell Building traversal,
distance arithmetic, queue copy and raw writes execute. Component, nearest-cell,
path estimate, and destination bodies are resolved adapters. Type +AF5/+D2C is false.
"""
import struct
import sys
import original_foot_path_check as m
from unicorn.x86_const import UC_X86_REG_EAX
original_inputs=m.inputs
BUILDING,BVT,KIND=0x100d000,0x100d800,0x10070d0
m.cpu.mem_write(KIND,b'\xb8\x06\x00\x00\x00\xc3')
def packed(cell): return cell[0]|(cell[1]<<16)
def inputs(seed):
    state=original_inputs(seed); x,y=state['goal']
    source=(x*256+128+1024+(seed%3)*512+seed%19-9,
            y*256+128+((seed//3)%3-1)*256+seed%23-11,state['source'][2])
    if seed%8==4: source=(x*256+128+1024,y*256+128,0)
    entry=7 if seed%3==0 else 6
    offsets=((0,0),(0,0),(1,0),(3,0),(4,0),(12,0),(-2,3),(2,-1))
    dx,dy=offsets[seed%8]; candidate=packed((x+dx,y+dy))
    if seed%8==0 and entry==6: candidate=state['invalid']
    gap=max(abs(dx),abs(dy)); estimate=(gap+6,gap+7,0x7fffffff,-1)[(seed//8)%4]
    state.update(source=source,entry=entry,present=seed%7!=0,movement_zone=seed%(12 if m.base else 13),
                 speed_type=(seed//5)%8,component=0xffff if seed%11==0 else 0xffffffff if seed%13==0 else 100+seed,
                 candidate=candidate,estimate=estimate,goal_bridge=seed%2!=0,candidate_bridge=(seed//2)%2!=0)
    return state
m.inputs=inputs
def distance():
    m.computed_distance()
    m.word(m.RULES+(0x1380 if m.base else 0x1718),576)
    m.word(m.TYPE+(0x500 if m.base else 0x5b4),m.state['movement_zone'])
    m.word(m.TYPE+(0x5a4 if m.base else 0x67c),m.state['speed_type'])
    m.cpu.mem_write(m.TYPE+(0xaf5 if m.base else 0xd2c),b'\0')
    m.cpu.mem_write(0xa40958 if m.base else 0xa8e9a0,b'\x01')
    m.word(m.CELL+0xe4,m.ACTOR); m.word(m.ACTOR+0x30,BUILDING)
    m.word(BUILDING,BVT); m.word(BUILDING+0x30,0); m.word(BVT+0x2c,KIND)
m.callbacks[0x4c3110 if m.base else 0x4d3a30]=distance
def lookup():
    cell=m.read(m.argument())
    m.events.append((10,cell,0,0)); m.word(m.CELL+0x24,cell)
    bridge=m.state['goal_bridge'] if cell==packed(m.state['goal']) else m.state['candidate_bridge']
    m.word(m.CELL+0x140,0x100 if bridge else 0); m.cpu.reg_write(UC_X86_REG_EAX,m.CELL)
m.callbacks[0x548070 if m.base else 0x5657a0]=lookup
def component():
    cell=m.read(m.argument()); zone=m.argument(1); layer=m.argument(2)&255
    m.events.append((11,cell,zone,layer)); m.cpu.reg_write(UC_X86_REG_EAX,m.state['component'])
m.bind(0x54f8f0 if m.base else 0x56d230,12,component)
def nearest():
    args=[m.argument(i) for i in range(14 if m.base else 15)]
    assert args[5]&255==int(m.state['above_bridge'])
    assert args[6:10]==[1,1,0,1] and args[10]&255==0 and args[11]&255==1 and args[13]&255==0
    if not m.base: assert args[14]&255==0
    m.events.append((12,m.read(args[1]),m.read(args[12]),args[2]))
    m.events.append((13,args[3],args[4],args[5]&255))
    m.word(args[0],m.state['candidate']); m.cpu.reg_write(UC_X86_REG_EAX,args[0])
m.bind(0x5502e0 if m.base else 0x56dc20,56 if m.base else 60,nearest)
def estimate():
    args=[m.argument(i) for i in range(6)]
    assert args[2]==m.ACTOR and args[5]==0xffffffff
    m.events.append((14,m.read(args[0]),m.read(args[1]),args[3]&255))
    m.events.append((15,args[4]&255,args[5],m.state['estimate']&0xffffffff))
    m.cpu.reg_write(UC_X86_REG_EAX,m.state['estimate']&0xffffffff)
m.bind(0x42aa30 if m.base else 0x42d170,24,estimate)
total=2166136261
for seed in range(512):
    digest=m.run(seed)
    values=(0xffffffff if m.state['destination'] is None else m.state['destination'],
            int(m.state['secondary']),0xffffffff if m.state['mission'] is None else m.state['mission'],
            m.read(m.failure_mode),m.cpu.mem_read(m.HOUSE+m.house_a,1)[0],m.cpu.mem_read(m.HOUSE+m.house_b,1)[0],
            *struct.unpack('<iii',m.cpu.mem_read(m.ACTOR+m.position,12)))
    for value in values: digest=((digest^(value&0xffffffff))*16777619)&0xffffffff
    if '--vectors' in sys.argv: print(seed,f'{digest:08x}')
    total=((total^digest)*16777619)&0xffffffff
assert total==(0xd51e380b if m.base else 0x22d0692f), 'original Foot retarget vectors changed'
print('512 original Foot retarget decisions',f'{total:08x}',file=sys.stderr)
