#!/usr/bin/env python3
"""Execute original land-table writes and category reads with distinct columns.

Usage: original_land_category_check.py executable [--base]
Only INI section, floating value, and boolean reads are supplied. The original
land reader writes its real table. The original category then reads that table.
The category usable-area answer is supplied as true.
"""
import struct
import sys
import original_category_check as category
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_FPCW
cpu=category.cpu; base=category.base
DATA=0x1006000
reader=0x64d610 if base else 0x674000
table=0x850ea0 if base else 0x89ea40
has_section=0x50c9b0 if base else 0x526810
read_double=0x50e3a0 if base else 0x5283d0
read_bool=0x50f410 if base else 0x5295f0
state={}; calls=[]
def word(address,value): category.word(address,value)
def argument(index=0): return struct.unpack('<I',cpu.mem_read(cpu.reg_read(UC_X86_REG_ESP)+4+4*index,4))[0]
def string(address): return bytes(cpu.mem_read(address,64)).split(b'\0',1)[0].decode('ascii')
def hook(cpu,address,size,user):
    if address==has_section:
        cpu.reg_write(UC_X86_REG_EAX,int(string(argument())=='Clear'))
    elif address==read_double:
        assert string(argument())=='Clear'
        assert struct.unpack('<d',cpu.mem_read(cpu.reg_read(UC_X86_REG_ESP)+12,8))[0]==1.0
        key=string(argument(1)); calls.append(key)
        cpu.mem_write(DATA,struct.pack('<d',state.get(key,0.25)))
    elif address==read_bool:
        assert string(argument())=='Clear' and string(argument(1))=='Buildable'
        cpu.reg_write(UC_X86_REG_EAX,0)
cpu.hook_add(UC_HOOK_CODE,hook)
cpu.mem_write(has_section,b'\xc2\x04\x00')
# This adapter returns the resolved INI double on the original x87 stack.
cpu.mem_write(read_double,b'\xdd\x05'+struct.pack('<I',DATA)+b'\xc2\x10\x00')
cpu.mem_write(read_bool,b'\xc2\x0c\x00')
cpu.reg_write(UC_X86_REG_FPCW,0xe7f)
profiles=((0.,1.),(1.,0.),(.125,.5),(.5,.125),(1.,1./128),(0.,1./64),(.5,-.25),(0.,1.5))
total=2166136261
for seed,(track,wheel) in enumerate(profiles):
    state={'Track':track,'Wheel':wheel}; calls=[]
    cpu.mem_write(table,bytes(12*36))
    cpu.mem_write(category.STACK,struct.pack('<II',category.STOP,0x1007000))
    cpu.reg_write(UC_X86_REG_ESP,category.STACK)
    cpu.emu_start(reader,category.STOP,count=10000)
    assert cpu.reg_read(UC_X86_REG_EIP)==category.STOP, 'original land reader exceeded bound'
    actual_track,actual_wheel=struct.unpack('<ff',cpu.mem_read(table+4,8))
    assert actual_track==min(track,1.) and actual_wheel==min(wheel,1.), 'original land reader column mismatch'
    assert 'Track' in calls and 'Wheel' in calls
    word(category.CELL+0x44,-1); word(category.CELL+0xec,0); word(category.CELL+0xe4,0)
    category.writes=[]; category.usable=True
    word(category.STACK,category.STOP)
    cpu.reg_write(UC_X86_REG_ESP,category.STACK); cpu.reg_write(UC_X86_REG_ECX,category.CELL)
    cpu.emu_start(0x478f40 if base else 0x483c80,category.STOP,count=10000)
    actual=struct.unpack('<I',cpu.mem_read(category.CELL+0x4c,4))[0]
    expected=6 if actual_wheel<=.01 else 0
    assert actual==expected and category.writes==[expected], 'category used a different land column'
    h=2166136261
    for value in (*struct.unpack('<II',cpu.mem_read(table+4,8)),actual): h=((h^value)*16777619)&0xffffffff
    total=((total^h)*16777619)&0xffffffff
    print(seed,f'{h:08x}')
assert total==0x8d749c93, 'original distinct-column vectors changed'
print(len(profiles),'original land-reader and category cases',f'{total:08x}',file=sys.stderr)
