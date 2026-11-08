#!/usr/bin/env python3
"""Compare native Battle starting-point selection with the original x86 code.

Requires Unicorn and the existing sim/original_image.py. Pass the original
executable and the chooser_cases binary. Only debug logging is stubbed. The
original random constructor, random draws, distance lookup, and chooser run.
"""
import pathlib
import struct
import subprocess
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_FPCW
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / 'sim'))
from original_image import load_original

if len(sys.argv) != 3:
    raise SystemExit('usage: original_chooser_check.py game.exe chooser_cases')
exe, native = sys.argv[1:]
base = pathlib.Path(exe).name.lower() == 'game.exe'
entry, constructor, logger = (0x5b7920, 0x638790, 0x4068f0) if base else (0x5d6890, 0x65c6d0, 0x4068e0)
scenario_global, houses_global, count_global = ((0xa3d290, 0xa3229c, 0xa3fa8c) if base else
                                                (0xa8b230, 0xa8022c, 0xa8da84))
cpu=load_original(exe)
cpu.mem_map(0x1000000,0x40000)
scenario, vector, points, flags = 0x1000000, 0x1004000, 0x1005000, 0x1006000
houses, output, stack, end = 0x1007000,0x1008000,0x103e000,0x103f000

def put(address, *values):
    cpu.mem_write(address,struct.pack('<'+'I'*len(values),*(value & 0xffffffff for value in values)))
def get(address, count=1):
    return struct.unpack('<'+'I'*count,cpu.mem_read(address,4*count))

put(scenario_global,scenario)
put(houses_global,houses)
put(count_global,0)
put(houses,0)
# Debug logging has no effect on game state; all game decisions execute.
cpu.mem_write(logger,b'\xc3')
lines=subprocess.check_output([native,exe],text=True).splitlines()
for line in lines:
    fields=list(map(int,line.split()))
    seed,count,mask,native_index,native_first,native_second,native_hash=fields[:7]
    coords=list(zip(fields[example.invalid],fields[example.invalid]))
    assert len(coords)==count
    put(stack,end,seed)
    cpu.reg_write(UC_X86_REG_ECX,scenario+0x218)
    cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.emu_start(constructor,end,count=100000)
    put(vector+4,points)
    put(vector+0x10,count)
    cpu.mem_write(points,b''.join(struct.pack('<hh',x,y) for x,y in coords))
    cpu.mem_write(flags,bytes(bool(mask&(1<<i)) for i in range(count)))
    for i in range(16):
        put(scenario+0x1180+4*i,0 if mask&(1<<i) else 0xffffffff)
    put(output,0)
    put(stack,end,output,0,vector,flags)
    cpu.reg_write(UC_X86_REG_ECX,0)
    cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.reg_write(UC_X86_REG_FPCW,0x0e7f)
    cpu.emu_start(entry,end,count=100000)
    chosen=struct.unpack('<hh',cpu.mem_read(output,4))
    expected=coords[native_index]
    assert chosen==expected,(hex(entry),seed,mask,chosen,expected)
    assert bytes(cpu.mem_read(flags,count))==bytes(bool(mask&(1<<i)) or i==native_index for i in range(count))
    first,second=get(scenario+0x218+4,2)
    table=get(scenario+0x218+12,250)
    state_hash=2166136261
    for word in table: state_hash=((state_hash^word)*16777619)&0xffffffff
    assert (first,second,state_hash)==(native_first,native_second,native_hash), (seed,mask)
print(f'{len(lines)} Battle chooser cases match original x86: {pathlib.Path(exe).name}')
