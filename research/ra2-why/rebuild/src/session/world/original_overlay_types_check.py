#!/usr/bin/env python3
"""Check base OverlayType registration for installed RULES.INI names.

Original 005DCF70 search, 005DC5A0 OverlayType constructor, 005D57F0
ObjectType constructor, and registry writes run. Parsed names, allocation,
the AbstractType constructor, and object registration are supplied. The
registry starts empty. Later mode reloads and live type state are not proved.
"""
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP
if len(sys.argv)!=4:
    raise SystemExit('usage: original_overlay_types_check.py game.exe overlay_rules_case game_dir')
exe,native,game=sys.argv[1:]
rows=[line.split() for line in subprocess.check_output([native,game],text=True).splitlines()]
assert len(rows)==243 and all(len(row)==19 for row in rows)
sys.argv=[sys.argv[0],exe,'--base']
import original_move_trace as m
objects=0x1400000;vector=0x1500000;nameptr=0x100a000
m.cpu.mem_map(objects,0x40000);m.cpu.mem_map(vector,0x1000)
m.word(0xa35dec,vector);m.word(0xa35df0,256);m.word(0xa35df8,0);m.word(0xa35dfc,0)
for address in (0xa72f98,0xa72fa0,0xa72fa4):m.word(address,0)
def string(address): return bytes(m.cpu.mem_read(address,64)).split(b'\0',1)[0].decode('ascii')
def abs_ctor():
    target=m.cpu.reg_read(UC_X86_REG_ECX)
    raw=string(m.argument(0)).encode('ascii')+b'\0'
    m.cpu.mem_write(target+0x24,raw)
    m.cpu.reg_write(UC_X86_REG_EAX,target)
created=[]
def allocate():
    assert m.argument(0)==0x288 and len(created)<len(rows)
    address=objects+len(created)*0x288
    m.cpu.mem_write(address,b'\xa5'*0x288)
    created.append(address);m.cpu.reg_write(UC_X86_REG_EAX,address)
m.bind(0x410580,4,abs_ctor);m.bind(0x40ffd0,4,m.ret(0));m.bind(0x782017,0,allocate)
for iteration in range(2):
    for index,row in enumerate(rows):
        name=row[1]
        m.cpu.mem_write(nameptr,name.encode('ascii')+b'\0')
        m.cpu.reg_write(UC_X86_REG_ECX,nameptr);m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
        m.cpu.mem_write(0x103f000,struct.pack('<I',0x103e000))
        m.cpu.emu_start(0x5dcf70,0x103e000,count=1000000)
        ptr=m.cpu.reg_read(UC_X86_REG_EAX)
        assert ptr==created[index] and m.read(vector+4*index)==ptr
        assert m.read(ptr+0x264)==index and string(ptr+0x24)==name
        assert m.read(0xa35df8)==(index+1 if iteration==0 else len(rows))
assert len(created)==len(rows)
print('base x86 registers 243 installed OverlayType names in order and reuses them on reload')
