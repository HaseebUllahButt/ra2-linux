#!/usr/bin/env python3
"""Run the original base packed-overlay scan on actual MP02T2 bytes.

The decoded asset stream is real. The type eligibility, map membership,
Cell lookup, memory allocation, and overlay constructor are supplied. Every
nonempty byte is admitted, so this is a scan check, not live overlay state.
"""
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBP,UC_X86_REG_ESP,UC_X86_REG_EIP
if len(sys.argv)!=4:
    raise SystemExit('usage: original_overlay_map_check.py game.exe real_map_case game_dir')
exe,emitter,game=sys.argv[1:]
data=subprocess.check_output([emitter,game,'--overlay-bytes'])
assert len(data)==512*512
sys.argv=[sys.argv[0],exe,'--base']
import original_move_trace as m
records=[]
cursor=[0]
def next_byte():
    index=cursor[0]
    assert index<len(data)
    m.cpu.mem_write(m.argument(0),data[index:index+1])
    cursor[0]+=1
def construct():
    coordinate=m.read(m.argument(1))
    x=coordinate&0xffff
    y=coordinate>>16
    assert x<512 and y<512
    records.append((x,y,data[y*512+x]))
    m.cpu.reg_write(UC_X86_REG_EAX,m.FOOT)
m.bind(0x535380,8,next_byte)
m.bind(0x54aab0,4,m.ret(1))
m.bind(0x782017,0,m.ret(m.FOOT))
m.bind(0x5da7d0,12,construct)
shared_cell=m.cell_for(20,20)
m.bind(0x548070,4,m.ret(shared_cell))
m.word(m.TYPE,m.VT)
m.virtual(0x9c,0,m.ret(1))
vector=0x1200000
m.cpu.mem_map(vector,0x1000)
for index in range(256):
    m.word(vector+index*4,m.TYPE)
m.word(0xa35dec,vector)
m.word(0xa3d298,0)
m.cpu.reg_write(UC_X86_REG_EBP,0)
m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
m.cpu.emu_start(0x5db780,0x5db8ae,count=50000000)
assert m.cpu.reg_read(UC_X86_REG_EIP)==0x5db8ae and cursor[0]==len(data)
expected=[(index%512,index//512,value) for index,value in enumerate(data) if value!=255]
assert len(expected)==407 and records==expected
assert all(abs(x-135)>8 or abs(y-80)>8 for x,y,value in records)
print(f'base x86 scans all {len(data)} MP02T2 overlay bytes and admits {len(records)} fixture objects')
