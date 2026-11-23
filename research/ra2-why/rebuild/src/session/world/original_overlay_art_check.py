#!/usr/bin/env python3
"""Compare Overlay art constructor and reader fields with base x86.

Runs the original OverlayType and ObjectType constructors, image reader slice,
and Theater/NewTheater/DemandLoad reader slices. Original INI, allocation,
and object registration services are supplied. Inputs are the installed base
rules and ART records for MP02T2's 17 packed Overlay indexes. Later writes and
complete runtime type loading are outside this check.
"""
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBP,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_ESI,UC_X86_REG_ESP
if len(sys.argv)!=4:
    raise SystemExit('usage: original_overlay_art_check.py game.exe overlay_shape_case game_dir')
exe,native,game=sys.argv[1:]
rows=[line.split() for line in subprocess.check_output([native,game,'--reader'],text=True).splitlines()]
assert len(rows)==17 and all(len(row)==10 for row in rows)
sys.argv=[sys.argv[0],exe,'--base']
import original_move_trace as m
vector=0x1200000;m.cpu.mem_map(vector,0x1000)
nameptr=0x100a000
m.word(0xa35dec,vector);m.word(0xa35df0,256);m.word(0xa35df8,0)
def string(address): return bytes(m.cpu.mem_read(address,128)).split(b'\0',1)[0].decode('ascii')
def abs_ctor():
    target=m.cpu.reg_read(UC_X86_REG_ECX)
    raw=string(m.argument(0)).encode('ascii')+b'\0'
    m.cpu.mem_write(target+0x24,raw)
    m.cpu.reg_write(UC_X86_REG_EAX,target)
m.bind(0x410580,4,abs_ctor);m.bind(0x40ffd0,4,m.ret(0))
current={}
def read_string():
    assert string(m.argument(1))=='Image'
    value=current['Image']
    if value=='-': value=string(m.argument(2))
    raw=value.encode('ascii')+b'\0'
    assert len(raw)<=m.argument(4)
    m.cpu.mem_write(m.argument(3),raw)
    m.cpu.reg_write(UC_X86_REG_EAX,len(raw)-1)
def read_boolean():
    key=string(m.argument(1));value=current.get(key,'-')
    if value=='-': result=m.argument(2)&255
    elif value.lower() in ('yes','true','1'): result=1
    elif value.lower() in ('no','false','0'): result=0
    else: raise AssertionError((key,value))
    m.cpu.reg_write(UC_X86_REG_EAX,result)
m.bind(0x50e9a0,20,read_string);m.bind(0x50f410,12,read_boolean)
for row in rows:
    index,name,rule_image,image,theater,new_theater,demand,*values=row
    current={'Image':rule_image,'Theater':theater,'NewTheater':new_theater,'DemandLoad':demand}
    m.cpu.mem_write(m.TYPE,b'\xa5'*0x400)
    m.cpu.mem_write(nameptr,name.encode('ascii')+b'\0')
    m.word(0xa35df8,0)
    m.cpu.reg_write(UC_X86_REG_ECX,m.TYPE);m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,nameptr))
    m.cpu.emu_start(0x5dc5a0,0x103e000,count=100000)
    assert string(m.TYPE+0x1c8)==name
    assert all(m.cpu.mem_read(m.TYPE+offset,1)==b'\0' for offset in (0x1fc,0x207,0x27f))
    m.cpu.reg_write(UC_X86_REG_EBX,m.TYPE);m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.emu_start(0x5d782b,0x5d7874,count=10000)
    assert string(m.TYPE+0x1c8)==image,(index,name,image,string(m.TYPE+0x1c8))
    m.cpu.reg_write(UC_X86_REG_EBX,m.TYPE);m.cpu.reg_write(UC_X86_REG_ESI,m.TYPE+0x1c8)
    m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.emu_start(0x5d7b10,0x5d7b50,count=10000)
    m.cpu.reg_write(UC_X86_REG_ESI,m.TYPE);m.cpu.reg_write(UC_X86_REG_EBP,m.TYPE+0x1c8)
    m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.emu_start(0x5dcbea,0x5dcc13,count=10000)
    actual=tuple(m.cpu.mem_read(m.TYPE+offset,1)[0] for offset in (0x1fc,0x207,0x27f))
    assert actual==tuple(map(int,values)),(index,name,actual,values)
print('base x86 Overlay constructors and art reader slices match 17 installed MP02T2 types')
