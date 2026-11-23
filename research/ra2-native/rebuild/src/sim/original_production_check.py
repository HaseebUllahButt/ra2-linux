#!/usr/bin/env python3
"""Compare the original prerequisite-list helper in both owned executables.

The original group, registry, inventory loops, and vector accessor execute.
Inputs contain supplied resolved BuildingType identities. This is not CanBuild.
Usage: original_production_check.py GAME_DIRECTORY NATIVE_TEST
"""
from pathlib import Path
import random
import struct
import subprocess
import sys
from original_image import load_original
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_EIP

if len(sys.argv) != 3:
    raise SystemExit("usage: original_production_check.py GAME_DIRECTORY NATIVE_TEST")
rng=random.Random(0x505360)
cases=[]
for group in range(6):
    for mask in range(256):
        cases.append(([-group-1], [i for i in range(8) if mask & (1<<i)],
                      [[i,(i+2)%8] for i in range(6)]))
cases.extend([([],[],[[] for _ in range(6)]),([0],[],[[] for _ in range(6)])])
for _ in range(2048):
    required=[rng.randrange(-6,8) for _ in range(rng.randrange(9))]
    available=[rng.randrange(8) for _ in range(rng.randrange(10))]
    groups=[[rng.randrange(8) for _ in range(rng.randrange(5))] for _ in range(6)]
    cases.append((required,available,groups))

def serialize(case):
    required,available,groups=case
    values=[len(required),*required,len(available),*available]
    for group in groups: values.extend([len(group),*group])
    return ' '.join(map(str,values))+'\n'
expected=list(map(int,subprocess.check_output([sys.argv[2],'--cases'],text=True,
    input=''.join(map(serialize,cases))).split()))
assert len(expected)==len(cases)
for edition,name in enumerate(('game.exe','gamemd.exe')):
    cpu=load_original(Path(sys.argv[1])/name)
    memory,stack,stop=0x1000000,0x103f000,0x103e000
    cpu.mem_map(memory,0x40000)
    kind,inventory,rules,registry=(memory+n for n in (0,0x2000,0x3000,0x5000))
    def word(address,value):cpu.mem_write(address,struct.pack('<I',value & 0xffffffff))
    def type_pointer(value):return memory+0x20000+value*0x100
    def pointers(address,values):
        for i,value in enumerate(values):word(address+4*i,type_pointer(value))
    if edition:
        entry,rules_pointer,registry_pointer,required_offset=0x505360,0x8871e0,0xa83c6c,0x63c
        group_offsets=(0x8c8,0x938,0x900,0xa34,0x91c,0x8e4)
        group_vtable=0x7eaa08
    else:
        entry,rules_pointer,registry_pointer,required_offset=0x4ef740,0x839848,0xa35cdc,0x564
        group_offsets=(0x7e4,0x854,0x81c,0x8e0,0x838,0x800)
        group_vtable=0x7a2f08
    word(rules_pointer,rules);word(registry_pointer,registry)
    pointers(registry,list(range(8)))
    for case,wanted in zip(cases,expected):
        required,available,groups=case
        required_data=memory+0x8000;available_data=memory+0x9000
        for i,value in enumerate(required):word(required_data+4*i,value)
        word(kind+required_offset,required_data);word(kind+required_offset+12,len(required))
        word(inventory+4,available_data);pointers(available_data,available)
        for i,(offset,group) in enumerate(zip(group_offsets,groups)):
            obj=rules+offset;data=memory+0xa000+i*0x100
            word(obj,group_vtable);word(obj+4,data);word(obj+16,len(group));pointers(data,group)
        # Both helpers receive all three arguments on the stack.
        cpu.mem_write(stack,struct.pack('<4I',stop,kind,inventory,len(available)))
        cpu.reg_write(UC_X86_REG_ESP,stack)
        cpu.emu_start(entry,stop,count=100000)
        assert cpu.reg_read(UC_X86_REG_EIP)==stop,'instruction bound exceeded'
        # The original returns a bool in AL; upper EAX bits are unspecified.
        actual=cpu.reg_read(UC_X86_REG_EAX) & 0xff
        assert actual==wanted,(name,case,actual,wanted)
    print(f'{name}: {len(cases)} prerequisite-list cases match original x86')
