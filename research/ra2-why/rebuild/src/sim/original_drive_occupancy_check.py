#!/usr/bin/env python3
"""Run base drive and Unit mark bodies with resolved external Cell callbacks.

This fixture executes the original drive wrapper, preparation, follow, position,
Unit mark, ordered links, and occupancy bodies. Existing explicit adapters
replace Cell setup, visibility, and map services. The world is synthetic.
"""
import contextlib
import io
import pathlib
import runpy
import struct
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP
if len(sys.argv)!=2:
    raise SystemExit('usage: original_drive_occupancy_check.py /path/to/game.exe')
exe=sys.argv[1];root=pathlib.Path(__file__).resolve().parent
sys.path.insert(0,str(root))
sys.argv=['original_chain_probe.py',exe,'--base','0','0','0']
with contextlib.redirect_stdout(io.StringIO()):
    fixture=runpy.run_path(str(root.parent/'session/world/original_chain_probe.py'))
r=fixture['r'];m=fixture['m'];cpu=m.cpu
slots=(0x11c,0x74,0x80,0x84,0x100,0xbc,0xe8,0x12c)
saved={slot:m.read(m.VT+slot) for slot in slots}
type_bytes=bytes(cpu.mem_read(m.TYPE,0x1000))
r.setup((4,4),15)
cpu.mem_write(m.TYPE,type_bytes);cpu.mem_write(m.FOOT+0x5a0,b'\x01')
for slot,value in saved.items():m.word(m.VT+slot,value)
def current_cell():
    x=m.signed(m.FOOT+0x88)//256;y=m.signed(m.FOOT+0x8c)//256
    m.word(0x1009000,x|(y<<16));cpu.reg_write(UC_X86_REG_EAX,0x1009000)
m.bind(0x1015000,4,current_cell);m.word(m.VT+0x1a0,0x1015000)
m.word(m.FOOT+0x564,m.LOCO+4);m.word(m.VT+0x19c,0x4ca160);m.word(m.VT+0x1b4,0x5d4860)
m.word(m.VT+0xec,0x7079e0);m.word(0xad057c,416)
m.word(m.FOOT+0x30,0);cpu.mem_write(m.FOOT+0x74,b'\x01');cpu.mem_write(m.FOOT+0x76,b'\x00')
m.word(m.TYPE+0xaa0,0);m.word(m.FOOT+0x370,0)
for x in range(52,57):
    for y in range(66,74):
        cell=m.cell_for(x,y);m.word(cell+0x140,0);m.word(cell+0x124,0);m.word(cell+0x128,0)
source=m.cell_for(54,69);m.word(source+0xe4,m.FOOT);m.word(source+0x124,32)
scenario=fixture['scenario'];random_before=bytes(cpu.mem_read(scenario+0x218,1012))
for frame in range(1,24):
    m.word(0xa40d2c,frame);cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,m.LOCO+4))
    cpu.emu_start(r.wrapper,0x103e000,count=200000)
    assert cpu.reg_read(UC_X86_REG_EIP)==0x103e000
    heads=[m.read(m.cell_for(54,y)+0xe4) for y in (69,70,71)]
    occupancy=[m.read(m.cell_for(54,y)+0x124) for y in (69,70,71)]
    current=0 if frame<=5 else 1 if frame<=16 else 2
    reserved=1 if frame<=11 else 2
    assert heads==[m.FOOT if i==current else 0 for i in range(3)],(frame,heads)
    assert occupancy==[32 if i==reserved else 0 for i in range(3)],(frame,occupancy)
    assert cpu.mem_read(m.FOOT+0x5a0,1)[0]==int(frame==23)
    assert bytes(cpu.mem_read(scenario+0x218,1012))==random_before
print('base original 23-frame drive preserves ordered heads and reserves the next Cell occupancy')
