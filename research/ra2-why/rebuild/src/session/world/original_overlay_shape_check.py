#!/usr/bin/env python3
"""Compare the base Overlay theater preload with real MP02T2 assets.

Original 005DC960 and its filename formatter run on the 17 supplied type
records. Their image names and Theater/DemandLoad flags are fixture inputs.
The original archive lookup is supplied with successful pointers; the native
side separately reads every named file from the installed MIX archives. Type
reader completion, archive cache state, fallback, and Overlay objects remain
outside this check.
"""
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP
if len(sys.argv)!=4:
    raise SystemExit('usage: original_overlay_shape_check.py game.exe overlay_shape_case game_dir')
exe,native,game=sys.argv[1:]
rows=[line.split() for line in subprocess.check_output([native,game],text=True).splitlines()]
assert len(rows)==17 and all(len(row)==4 and int(row[3])>0 for row in rows)
sys.argv=[sys.argv[0],exe,'--base']
import original_move_trace as m
objects=0x1400000;vector=0x1500000
m.cpu.mem_map(objects,0x20000);m.cpu.mem_map(vector,0x1000)
m.word(0xa35dec,vector);m.word(0xa35df8,len(rows))
for i,(_,image,_,_) in enumerate(rows):
    address=objects+i*0x400
    m.word(vector+i*4,address)
    m.cpu.mem_write(address+0x1c8,image.encode('ascii')+b'\0')
    m.cpu.mem_write(address+0x1fc,b'\1')
    m.cpu.mem_write(address+0x27f,b'\0')
names=[]
def lookup():
    pointer=m.cpu.reg_read(UC_X86_REG_ECX)
    name=bytes(m.cpu.mem_read(pointer,256)).split(b'\0',1)[0].decode('ascii')
    names.append(name)
    m.cpu.reg_write(UC_X86_REG_EAX,0x1020000+len(names)*16)
m.bind(0x5956c0,0,lookup)
m.cpu.reg_write(UC_X86_REG_ECX,0)
m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
m.cpu.mem_write(0x103f000,struct.pack('<I',0x103e000))
m.cpu.emu_start(0x5dc960,0x103e000,count=1000000)
assert names==[row[2] for row in rows],(names,rows)
assert all(m.read(objects+i*0x400+0xa4)==0x1020000+(i+1)*16 for i in range(len(rows)))
print('base x86 theater preload names match 17 installed MP02T2 Overlay assets')
