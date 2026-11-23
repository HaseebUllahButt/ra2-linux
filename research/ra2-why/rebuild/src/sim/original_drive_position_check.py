#!/usr/bin/env python3
"""Check base drive position-virtual order with the existing resolved fixture.

Original drive wrapper, track preparation, and track following execute. The
fixture replaces position and other Foot callbacks with explicit adapters.
"""
import struct
import sys
from unicorn.x86_const import UC_X86_REG_EIP,UC_X86_REG_ESP
if len(sys.argv)!=2:
    raise SystemExit('usage: original_drive_position_check.py /path/to/game.exe')
exe=sys.argv[1]
sys.argv=['original_route_check.py',exe,'--base']
import original_route_check as route
m=route.m;cpu=m.cpu
route.setup((4,4),15)
original=m.location;events=[];frame=0
def position():
    p=m.argument();events.append((frame,m.signed(p)-13952,m.signed(p+4)-17792,m.signed(p+8)))
    original()
m.virtual(0x19c,4,position)
for frame in range(1,24):
    m.word(0xa40d2c,frame);cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,m.LOCO+4))
    cpu.emu_start(route.wrapper,0x103e000,count=100000)
    assert cpu.reg_read(UC_X86_REG_EIP)==0x103e000
hash_value=2166136261
for event in events:
    for value in event: hash_value=((hash_value^(value&0xffffffff))*16777619)&0xffffffff
assert (len(events),hash_value)==(68,0x0f94096c),(len(events),hex(hash_value))
print('base original drive makes 68 ordered position calls across 23 straight-route frames')
