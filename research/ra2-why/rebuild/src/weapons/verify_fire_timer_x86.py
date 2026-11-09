#!/usr/bin/env python3
"""Compare the base firing timer gate with the owned base executable.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/fire_timer{,_test}.cpp rebuild/src/sim/random.cpp -o /tmp/ra2_fire_timer
Run: RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_fire_timer_x86.py /tmp/ra2_fire_timer
"""
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import *
u=Uc(UC_ARCH_X86,UC_MODE_32)
u.mem_map(0x400000,0x800000)
u.mem_write(0x400000,(Path(os.environ['RA2_GAME_DIR'])/'game.exe').read_bytes()[:0x431000])
u.mem_map(0x1000000,0x10000)
obj=0x1001000
result=None
def word(p,n): u.mem_write(p,struct.pack('<i',n))
def hook(cpu,address,size,user):
    global result
    result=int(address==0x6c96c2)
    cpu.emu_stop()
for addr in (0x6c96b3,0x6c96c2): u.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
rng=random.Random(0x6c9690)
cases=[]
for _ in range(4096):
    start=rng.choice((-1,0,1,20,100,10000))
    duration=rng.choice((0,1,2,10,65,500))
    now=rng.choice((0,1,start-1,start,start+duration-1,start+duration,start+duration+1,10000))
    cases.append((start,duration,now))
lines=subprocess.check_output([sys.argv[1]],input=''.join(' '.join(map(str,c))+'\n' for c in cases),text=True).splitlines()
for case,line in zip(cases,lines):
    start,duration,now=case
    word(obj+0x244,start); word(obj+0x24c,duration); word(0xa40d2c,now)
    u.reg_write(UC_X86_REG_ESI,obj); result=None
    u.emu_start(0x6c9690,0x6c96c3,count=100)
    assert result==int(line),(case,result,line)
assert len(lines)==len(cases)
print(f'Base game.exe: {len(cases)} firing timer decisions match')
