#!/usr/bin/env python3
"""Compare real base Unit mark-out/in, ordered links, and occupancy words.

The existing chain fixture supplies map lookups and Cell setup callbacks. The
Foot, Object, Techno, Cell link, and Unit occupancy instructions run from the
original game.exe. The fixture does not execute complete Cell setup effects.
"""
import contextlib
import io
import pathlib
import runpy
import struct
import sys
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

if len(sys.argv)!=2:
    raise SystemExit('usage: original_unit_mark_check.py /path/to/game.exe')
exe=sys.argv[1]
root=pathlib.Path(__file__).resolve().parent
sys.path.insert(0,str(root))
sys.argv=['original_chain_probe.py',exe,'--base','0','0','0']
with contextlib.redirect_stdout(io.StringIO()):
    fixture=runpy.run_path(str(root.parent/'session/world/original_chain_probe.py'))
m=fixture['m']; cpu=m.cpu
m.word(m.VT+0xec,0x7079e0) # Actual Unit remove-occupancy virtual.
m.word(0xad057c,416) # Original bridge-height initializer; see original_floor_check.py.
cell=m.cell_for(54,69);other=0x1030000
m.word(other+0x30,m.FOOT);m.word(m.FOOT+0x30,0)
m.word(cell+0xe4,other);m.word(cell+0x124,0xa5a50020)
scenario=fixture['scenario']; before=bytes(cpu.mem_read(scenario+0x218,1012))
for mode,head,object_next,other_next,occupancy,marked in (
    (0,other,0,0,0xa5a50000,0),
    (1,m.FOOT,other,0,0xa5a50020,1),
):
    previous=len(fixture['setup_calls'])
    cpu.reg_write(UC_X86_REG_ECX,m.FOOT);cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,mode))
    cpu.emu_start(0x4c2e60,0x103e000,count=100000)
    assert cpu.reg_read(UC_X86_REG_EIP)==0x103e000
    assert cpu.reg_read(UC_X86_REG_EAX)&255==1
    assert (m.read(cell+0xe4),m.read(m.FOOT+0x30),m.read(other+0x30),m.read(cell+0x124),
            cpu.mem_read(m.FOOT+0x74,1)[0])==(head,object_next,other_next,occupancy,marked)
    assert len(fixture['setup_calls'])==previous+1 and fixture['setup_calls'][-1]==cell
    assert bytes(cpu.mem_read(scenario+0x218,1012))==before
print('base original Unit mark-out/in preserves ordered links, full occupancy words, and random state')
