#!/usr/bin/env python3
"""Check original ground goal-wrapper control with resolved helper answers.
Map/type queries and search helpers are explicit adapters. Original branch and retry control executes.
"""
import struct
import sys
import original_route_check as r
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
m=r.m; cpu=m.cpu; base=m.base
SEARCH,SVT=0x1200000,0x1202000
cpu.mem_map(SEARCH,0x10000)
START,END=0x100d000,0x100d004
state={}; events=[]; hierarchy_calls=regular_calls=retries=0

def reset(): events.append((0,0,0))
def clear():
    level=(cpu.reg_read(UC_X86_REG_ECX)-SEARCH-0x74)//24
    assert level in (0,1,2)
    events.append((1+level,0,0))
m.bind(0x427e90 if base else 0x42a5b0,0,reset)
m.bind(0x1202100,0,clear)

def component():
    index=0 if m.argument()==START else 1
    cpu.reg_write(UC_X86_REG_EAX,17+(state['different'] and index))
m.bind(0x54f8f0 if base else 0x56d230,12,component)
def ground_point():
    output,cell=m.argument(),m.argument(1)
    m.word(output,m.read(cell+0x24)); cpu.reg_write(UC_X86_REG_EAX,output)
m.bind(0x564f70 if base else 0x583180,12,ground_point)
def usable(): cpu.reg_write(UC_X86_REG_EAX,int(state['source_usable'] if m.read(m.argument())==m.read(START) else state['goal_usable']))
m.bind(0x55a660 if base else 0x578460,8,usable)

def hierarchy():
    global hierarchy_calls
    events.append((4,0,0))
    answer=state['hierarchy'][min(hierarchy_calls,len(state['hierarchy'])-1)]; hierarchy_calls+=1
    cpu.reg_write(UC_X86_REG_EAX,int(answer))
m.bind(0x429b60 if base else 0x42c290,16,hierarchy)

def regular():
    global regular_calls
    enabled=m.argument(5)&255; limit=m.argument(4); limit=limit if limit<0x80000000 else limit-0x100000000
    events.append((5,enabled,limit))
    answer=state['regular'][min(regular_calls,len(state['regular'])-1)]; regular_calls+=1
    cpu.reg_write(UC_X86_REG_EAX,0x1203000 if answer else 0)
m.bind(0x427370 if base else 0x429a90,24,regular)
def retry():
    global retries
    events.append((6,0,0))
    answer=state['retry'][min(retries,len(state['retry'])-1)]; retries+=1
    cpu.mem_write(SEARCH+0x38,bytes([answer]))
m.bind(0x42a590 if base else 0x42ccd0,4,retry)
m.bind(0x4068f0 if base else 0x4068e0,0,m.ret(0)) # Diagnostic output has no gameplay state.

def inputs(seed):
    value=seed^0x9264ba17
    def next():
        nonlocal value
        value=(value*1664525+1013904223)&0xffffffff
        return value>>16
    flags=next(); limit=(-2,-1,0,1,500)[next()%5]
    hs=[next()%2 for _ in range(6)]; regular_answers=[next()%2 for _ in range(6)]; retry_answers=[next()%2 for _ in range(6)]
    if seed<10:
        flags=0x1a # on-map, source usable, goal usable; train and guard clear.
        limit=-1 if seed%2==0 else 10
        hs=[1]*6; regular_answers=[0]*6; retry_answers=[1]*6
        if seed in (2,3): hs[0]=0
        if seed in (4,5): retry_answers[0]=0
        if seed in (6,7): hs[1]=0
        if seed in (8,9): regular_answers[1]=1
    return dict(train=bool(flags&1),on_map=bool(flags&2),guard=bool(flags&4),source_usable=bool(flags&8),goal_usable=bool(flags&16),different=bool(flags&32),limit=limit,hierarchy=hs,regular=regular_answers,retry=retry_answers)

def run(seed):
    global state,events,hierarchy_calls,regular_calls,retries
    state=inputs(seed); events=[]; hierarchy_calls=regular_calls=retries=0
    r.setup((0,),15)
    cpu.mem_write(SEARCH,bytes(0x1000)); m.word(SVT+12,0x1202100)
    for level in range(3): m.word(SEARCH+0x74+level*24,SVT)
    cpu.mem_write(m.TYPE+(0xaa0 if base else 0xc94),bytes([state['train']]))
    cpu.mem_write(m.FOOT+(0x32d if base else 0x3d5),bytes([state['on_map']]))
    m.virtual(0x2f4 if base else 0x320,0,m.ret(state['guard']))
    m.word(START,54|(69<<16)); m.word(END,56|(67<<16))
    cpu.reg_write(UC_X86_REG_ECX,SEARCH); cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    cpu.mem_write(0x103f000,struct.pack('<8I',0x103e000,START,END,m.FOOT,0x100e000,state['limit']&0xffffffff,0,0))
    cpu.emu_start(0x42a1d0 if base else 0x42c900,0x103e000,count=100000)
    assert cpu.reg_read(UC_X86_REG_EIP)==0x103e000 and cpu.reg_read(UC_X86_REG_ESP)==0x103f020,'original wrapper did not finish'
    found=bool(cpu.reg_read(UC_X86_REG_EAX))
    value=2166136261
    for item in (int(found),int(cpu.mem_read(SEARCH+0x38,1)[0]!=0),len(events),*[item for event in events for item in event]): value=((value^(item&0xffffffff))*16777619)&0xffffffff
    return value,regular_calls,hierarchy_calls,retries

if __name__=='__main__':
    for seed in range(512):
        value,regular_count,hierarchy_count,retry_count=run(seed)
        if '--vectors' in sys.argv: print('{%d,0x%08xu,%d,%d,%d},'%(seed,value,regular_count,hierarchy_count,retry_count))
        else: print('original goal control',seed,'%08X'%value,regular_count,hierarchy_count,retry_count)
