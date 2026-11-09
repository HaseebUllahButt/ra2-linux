#!/usr/bin/env python3
"""Check the Unit unlimbo suffix with the parent placement result supplied."""
import pathlib
import struct
import subprocess
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / 'sim'))
from original_image import load_original

if len(sys.argv)!=3:
    raise SystemExit('usage: original_unlimbo_check.py game.exe unlimbo_cases')
exe,native=sys.argv[1:]
base=pathlib.Path(exe).name.lower()=='game.exe'
entry,parent,turn,ctor,global_scenario,frame=((0x6fc940,0x4c5e40,0x4b8b10,0x638790,0xa3d290,0xa40d2c) if base else
                                            (0x737ba0,0x4d7170,0x4c9300,0x65c6d0,0xa8b230,0xa8ed84))
type_ptr,unit_flag,suppress,status,timer,stamp,active=((0x5ac,0x32a,0x32d,0x1b8,0xe0,0xe4,0xf0) if base else
                                                       (0x6c4,0x3d2,0x3d5,0x220,0xf8,0x100,0x10c))
type_flag=0xbb0 if base else 0xe18
cpu=load_original(exe)
cpu.mem_map(0x1000000,0x40000)
scenario,unit,unit_type,point,stack,end=0x1000000,0x1002000,0x1004000,0x1006000,0x103e000,0x103f000
def put(address,*values):
    cpu.mem_write(address,struct.pack('<'+'I'*len(values),*(value&0xffffffff for value in values)))
def get(address,n=1): return struct.unpack('<'+'I'*n,cpu.mem_read(address,4*n))
put(global_scenario,scenario); put(frame,1234)
cpu.mem_write(parent,b'\xc2\x08\x00')
cpu.mem_write(turn,b'\xc2\x04\x00')
seen=[]
success=False
def hook(machine,address,size,user):
    if address==parent:
        machine.reg_write(UC_X86_REG_EAX,int(success))
    if address==turn:
        esp=machine.reg_read(UC_X86_REG_ESP)
        address_arg=get(esp+4)[0]
        seen.append(struct.unpack('<H',machine.mem_read(address_arg,2))[0])
cpu.hook_add(UC_HOOK_CODE,hook)
lines=subprocess.check_output([native],text=True).splitlines()
for line in lines:
    values=list(map(int,line.split()))
    seed,facing,flags,turn_value,expected_status,length,enabled,first,second=values[:9]
    expected_table=tuple(values[9:]); assert len(expected_table)==250
    put(stack,end,seed)
    cpu.reg_write(UC_X86_REG_ECX,scenario+0x218);cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.emu_start(ctor,end,count=100000)
    put(unit+type_ptr,unit_type);put(unit+status,77)
    cpu.mem_write(unit+unit_flag,bytes([bool(flags&1)]))
    cpu.mem_write(unit+suppress,bytes([bool(flags&2)]))
    cpu.mem_write(unit_type+type_flag,bytes([bool(flags&4),bool(flags&8)]))
    put(stack,end,point,facing)
    original_unit=bytes(cpu.mem_read(unit,0x1000))
    original_rng=bytes(cpu.mem_read(scenario+0x218,1012))
    success=False
    cpu.reg_write(UC_X86_REG_ECX,unit);cpu.reg_write(UC_X86_REG_ESP,stack)
    seen.clear();cpu.emu_start(entry,end,count=100000)
    assert cpu.reg_read(UC_X86_REG_EAX)&0xff==0 and not seen
    assert bytes(cpu.mem_read(unit,0x1000))==original_unit
    assert bytes(cpu.mem_read(scenario+0x218,1012))==original_rng
    success=True
    put(stack,end,point,facing)
    cpu.reg_write(UC_X86_REG_ECX,unit);cpu.reg_write(UC_X86_REG_ESP,stack)
    seen.clear();cpu.emu_start(entry,end,count=100000)
    assert seen==[turn_value],(values[:9],seen)
    assert get(unit+status)==(expected_status,)
    assert get(unit+timer)==(length,) and get(unit+active)==(enabled,)
    assert get(unit+stamp)==(1234,) and get(unit+stamp+8)==(enabled,)
    assert get(scenario+0x218+4,2)==(first,second)
    assert get(scenario+0x218+12,250)==expected_table
print(f'{len(lines)} Unit unlimbo suffix cases match original x86: {pathlib.Path(exe).name}')
