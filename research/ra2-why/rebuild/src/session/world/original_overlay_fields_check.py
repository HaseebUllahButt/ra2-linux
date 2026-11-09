#!/usr/bin/env python3
"""Compare one OverlayType rule-reader slice against original base x86.

The installed ordered names and authored values come from RULES.INI. The
ObjectType reader, INI services, Land-name service, animation lookup, art
service, and unrelated type fields are supplied. Original reader control flow
and the +268/+278/+279/+27A writes execute. Later mode and map layers remain
outside this check; these results are not live OverlayType state.
"""
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBP,UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_ESP
if len(sys.argv)!=4:
    raise SystemExit('usage: original_overlay_fields_check.py game.exe overlay_rules_case game_dir')
exe,native,game=sys.argv[1:]
rows=[line.split() for line in subprocess.check_output([native,game],text=True).splitlines()]
assert len(rows)==243 and all(len(row)==19 for row in rows)
sys.argv=[sys.argv[0],exe,'--base']
import original_move_trace as m
lands=('clear','road','water','rock','wall','tiberium','beach','rough','ice','railroad','tunnel','weeds')
def string(address):
    return bytes(m.cpu.mem_read(address,64)).split(b'\0',1)[0].decode('ascii')
current={}
def land():
    assert string(m.argument(1))=='Land'
    value=current.get('Land','-')
    result=m.argument(2) if value=='-' else (lands.index(value.lower()) if value.lower() in lands else -1)
    m.cpu.reg_write(UC_X86_REG_EAX,result & 0xffffffff)
def boolean():
    key=string(m.argument(1)); value=current.get(key,'-')
    if value=='-': result=m.argument(2)&255
    elif value.lower() in ('yes','true','1'): result=1
    elif value.lower() in ('no','false','0'): result=0
    else: raise AssertionError((key,value))
    m.cpu.reg_write(UC_X86_REG_EAX,result)
def fallback_integer(): m.cpu.reg_write(UC_X86_REG_EAX,m.argument(2))
def no_animation():
    assert string(m.argument(1))=='CellAnim'
    m.cpu.reg_write(UC_X86_REG_EAX,0)
m.bind(0x46a970,12,land)
m.bind(0x50f410,12,boolean)
m.bind(0x50d7f0,12,fallback_integer)
m.bind(0x50e9a0,20,no_animation)
m.bind(0x5956c0,0,m.ret(0))
checked=0
for row in rows:
    index,name,present,land_value,wall,tiberium,crate,no_tile,rock,rubble,crushable,*native_values=row
    if present=='0': continue
    current={'Land':land_value,'Wall':wall,'Tiberium':tiberium,'Crate':crate,
             'NoUseTileLandType':no_tile,'IsARock':rock,'IsRubble':rubble,'Crushable':crushable}
    m.cpu.mem_write(m.TYPE,bytes(0x400))
    m.cpu.mem_write(m.TYPE+0x24,name.encode('ascii')+b'\0')
    m.cpu.mem_write(m.TYPE+0x1c8,name.encode('ascii')+b'\0')
    m.cpu.reg_write(UC_X86_REG_EBX,m.TYPE);m.cpu.reg_write(UC_X86_REG_EBP,m.TYPE+0x24)
    m.cpu.reg_write(UC_X86_REG_ESI,0x100b000);m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.emu_start(0x5d7931,0x5d7960,count=10000)
    m.cpu.reg_write(UC_X86_REG_ESI,m.TYPE);m.cpu.reg_write(UC_X86_REG_EBX,0x100b000)
    m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.emu_start(0x5dcabf,0x5dcd26,count=10000)
    actual=(m.signed(m.TYPE+0x268),*(m.cpu.mem_read(m.TYPE+offset,1)[0] for offset in (0x278,0x279,0x27a,0x27c,0x285,0x284,0x1fd)))
    assert actual==tuple(map(int,native_values)),(index,name,actual,native_values)
    checked+=1
assert checked>100
print(f'base x86 OverlayType reader slice matches {checked} installed rule sections with supplied INI services')
