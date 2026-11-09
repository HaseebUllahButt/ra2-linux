#!/usr/bin/env python3
"""Recover a flat original drive segment with explicit map and Foot adapters.
Development evidence harness. It does not execute the game or claim full callback recovery.
Requires Unicorn. Usage: original_move_trace.py /path/to/executable [--base] [--vectors]
"""
import struct
import sys
from unicorn import UC_HOOK_CODE, UcError
from original_image import load_original
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_FPCW
base = '--base' in sys.argv
cpu = load_original(sys.argv[1])
position = 0x88 if base else 0x9c
alive = 0x7d if base else 0x90
cpu.mem_map(0x1000000,0x40000)
cpu.reg_write(UC_X86_REG_FPCW,0xe7f)
LOCO, FOOT, TYPE, VT = 0x1000000,0x1001000,0x1003000,0x1005000

def word(address,value): cpu.mem_write(address,struct.pack('<I',value & 0xffffffff))
def read(address): return struct.unpack('<I',cpu.mem_read(address,4))[0]
def signed(address): return struct.unpack('<i',cpu.mem_read(address,4))[0]
def coord(address,value): cpu.mem_write(address,struct.pack('<iii',*value))
callbacks = {}
next_stub=0x1010000

def bind(address,pop,callback):
    callbacks[address]=callback
    cpu.mem_write(address,b'\xc2'+struct.pack('<H',pop) if pop else b'\xc3')

def virtual(slot,pop,callback):
    global next_stub
    address=next_stub; next_stub+=16
    word(VT+slot,address)
    bind(address,pop,callback)

def ret(value): return lambda: cpu.reg_write(UC_X86_REG_EAX,value)
def argument(n=0): return read(cpu.reg_read(UC_X86_REG_ESP)+4+4*n)

def location():
    point=argument()
    cpu.mem_write(FOOT+position,bytes(cpu.mem_read(point,12)))
    word(0x1009000,(signed(FOOT+position)//256) | ((signed(FOOT+position+4)//256)<<16))

cells={}
def cell_for(x,y):
    if (x,y) not in cells:
        address=0x1020000+len(cells)*0x200
        cells[x,y]=address
        word(address+0x24,(x&65535)|((y&65535)<<16))
        word(address+0x44,-1)
        word(address+0xec,0)
    return cells[x,y]
def map_coord():
    point=argument()
    cpu.reg_write(UC_X86_REG_EAX,cell_for(signed(point)//256,signed(point+4)//256))
def map_cell():
    pair=read(argument()); x=pair&65535; y=pair>>16
    cpu.reg_write(UC_X86_REG_EAX,cell_for(x,y))

recent=[]
def hook(cpu,address,size,user):
    recent.append(address)
    if len(recent)>16: recent.pop(0)
    if address in callbacks: callbacks[address]()
cpu.hook_add(UC_HOOK_CODE,hook)
def setup(facing,speed):
    cpu.mem_write(LOCO,b'\0'*0x4000)
    word(FOOT,VT); word(FOOT+(0x5ac if base else 0x6c4),TYPE)
    word(FOOT+(0x58 if base else 0x6c),400); word(TYPE+(0x8c if base else 0xa0),400)
    cpu.mem_write(FOOT+alive,b'\x01')
    path=0x4d0 if base else 0x5e0
    word(FOOT+path,-1); word(FOOT+path+4,-1)
    word(FOOT+(0x5b4 if base else 0x6cc),-1)
    source=(13952,17792,0)
    directions=((0,-1),(1,-1),(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1))
    dx,dy=directions[facing//32]
    destination=(source[0]+dx*256,source[1]+dy*256,0)
    coord(FOOT+position,source)
    word(0x1009000,54|(69<<16))
    word(LOCO+0xc,FOOT); coord(LOCO+0x34,destination); coord(LOCO+0x40,destination)
    word(LOCO+0x58,(facing//32)*9); cpu.mem_write(LOCO+0x63,b'\x01')
    cpu.mem_write(LOCO+0x50,struct.pack('<d',1.))
    virtual(0x80 if base else 0x84,0,ret(TYPE))
    virtual(0xbc if base else 0xc0,0,ret(0))
    virtual(0x4bc if base else 0x538,0,ret(speed))
    virtual(0x4c8 if base else 0x544,8,ret(0))
    virtual(0x11c if base else 0x124,4,ret(1))
    virtual(0x19c if base else 0x1b4,4,location)
    virtual(0x1a0 if base else 0x1b8,4,ret(0x1009000))
    virtual(0x1b4 if base else 0x1cc,4,ret(0))
    virtual(0x174 if base else 0x18c,4,ret(0))
    virtual(0x490 if base else 0x504,0,ret(0))
    virtual(0x2c,0,ret(1))
    virtual(0x48,4,ret(FOOT+position))
    return destination
bind(0x548000 if base else 0x565730,4,map_coord)
bind(0x548070 if base else 0x5657a0,4,map_cell)
bind(0x55a280 if base else 0x578080,4,ret(0))
bind(0x476920 if base else 0x481a00,0,ret(0))
bind(0x4b8b10 if base else 0x4c9300,4,ret(0))
bind(0x6d7910 if base else 0x70d0d0,4,ret(0))
if __name__ == '__main__':
    for facing in range(0,256,32):
        for speed in (1,7,15,30,255):
            destination=setup(facing,speed)
            h=2166136261
            for frame in range(1,1025):
                cpu.reg_write(UC_X86_REG_ECX,LOCO)
                cpu.reg_write(UC_X86_REG_ESP,0x103f000)
                cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0))
                try: cpu.emu_start(0x4a1000 if base else 0x4b0f20,0x103e000,count=100000)
                except UcError:
                    print('unresolved original dependency at',hex(cpu.reg_read(UC_X86_REG_EIP)),[hex(a) for a in recent],hex(read(cpu.reg_read(UC_X86_REG_ESP))))
                    raise
                current=tuple(signed(FOOT+position+4*i) for i in range(3))
                state=(*current,signed(LOCO+0x58),read(LOCO+0x5c),read(LOCO+0x4c))
                if '--trace' in sys.argv and facing==int(sys.argv[-2]) and speed==int(sys.argv[-1]): print(frame,*state)
                for value in state: h=((h ^ (value & 0xffffffff))*16777619)&0xffffffff
                if signed(LOCO+0x58)==-1:
                    assert current==destination, (facing,speed,current,destination)
                    break
            else: raise AssertionError('original move did not finish')
            if '--vectors' in sys.argv: print('{%d,%d,%d,0x%08xu},'%(facing,speed,frame,h))
            else: print('original Move facing %d speed %d frames %d hash %08x'%(facing,speed,frame,h))
