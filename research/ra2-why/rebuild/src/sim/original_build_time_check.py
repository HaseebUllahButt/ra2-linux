#!/usr/bin/env python3
"""Compare native build times with original base 006C22A0 and Yuri 006F47A0.

The original type cost timer, power ratio, truncation, power clamps, multiple
factory loop and wall multiplier execute unchanged. Two house helpers supply
explicit country-time and factory-count inputs. This does not prove queues,
prerequisites, factory placement, or those two input bindings.
Usage: original_build_time_check.py GAME_DIRECTORY NATIVE_BUILD_TIME_TEST
"""
from pathlib import Path
import random
import struct
import subprocess
import sys
from original_image import load_original
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_FPCW

if len(sys.argv) != 3:
    raise SystemExit("usage: original_build_time_check.py GAME_DIRECTORY NATIVE_BUILD_TIME_TEST")
rng = random.Random(0x6c22a0)
cases = [(1000,0.7,1,0.5,100,100,0.5,0.8,1,0.8,1,0,3),
         (1000,0.7,1,0.5,99,100,0.5,0.8,1,0.8,1,0,3),
         (1000,0.7,1,0.5,100,100,0.5,0.8,1,0.8,2,0,3),
         (1000,0.7,1,0.5,100,100,0.5,0.8,1,0.8,1,1,3)]
for _ in range(2048):
    cases.append((rng.choice((0,1,99,100,200,700,1000,2000,5000)),
                  rng.choice((0,0.5,0.7,1,2.5)), rng.choice((0,0.5,0.8,1,1.3,2)),
                  rng.choice((0,0.7,1,1.5)), rng.choice((0,1,30,99,100,150,200)),
                  rng.choice((0,1,50,100,200)), rng.choice((0,0.25,0.5)),
                  rng.choice((0.5,0.8,1)), rng.choice((0,0.5,1,2)),
                  rng.choice((0,0.5,0.8,1,1.2)), rng.randrange(9), rng.randrange(2),
                  rng.choice((0.5,1,3))))

for edition, name in enumerate(("game.exe", "gamemd.exe")):
    native = list(map(int, subprocess.check_output([sys.argv[2]], text=True,
        input="".join(" ".join(map(str,(edition,*case)))+"\n" for case in cases)).split()))
    assert len(native) == len(cases)
    cpu = load_original(Path(sys.argv[1])/name)
    memory, stack, stop = 0x1000000, 0x103f000, 0x103e000
    cpu.mem_map(memory, 0x40000)
    obj, kind, house, rules, vtable, type_table = (memory+n for n in (0,0x1000,0x2000,0x9000,0xb000,0xc000))
    def word(address,value):
        cpu.mem_write(address,struct.pack("<I",value & 0xffffffff))
    def single(address,value):
        cpu.mem_write(address,struct.pack("<f",value))
    def double(address,value):
        cpu.mem_write(address,struct.pack("<d",value))
    def returns(address,value,pop=0):
        cpu.mem_write(address,b'\xb8'+struct.pack('<I',value)+
                      (b'\xc2'+struct.pack('<H',pop) if pop else b'\xc3'))
    if edition:
        entry, getter, owner, type_slot, first_slot = 0x6f47a0,0x711ee0,0x21c,0x84,0x88
        country, factories, rules_pointer = 0x50c0a0,0x500910,0x8871e0
        cost_offset, speed_offset, power_offset = 0x610,0x1748,0x53a4
        minimum, maximum, penalty, multiple, wall_scale = 0x570,0x574,0x578,0x57c,0x758
        building, wall_offset = 0x520,0x1571
    else:
        entry, getter, owner, type_slot, first_slot = 0x6c22a0,0x6dad10,0x1b4,0x80,0x84
        country, factories, rules_pointer = 0x4f5a70,0x4eb980,0x839848
        cost_offset, speed_offset, power_offset = 0x550,0x13b0,0x52d0
        minimum, maximum, penalty, multiple, wall_scale = 0x490,0x494,0x498,0x49c,0x678
        building, wall_offset = 0x418,0x1215
    word(obj,vtable)
    word(kind,type_table)
    word(obj+owner,house)
    word(obj+building,kind)
    word(rules_pointer,rules)
    returns(memory+0xd000,kind)
    word(vtable+type_slot,memory+0xd000)
    word(vtable+first_slot,memory+0xd000)
    word(vtable+0x2c,memory+0xd100)
    word(type_table+0x88,getter)
    # Resolved external country multiplier; preserve the x87 float return.
    cpu.mem_write(country,b'\xd9\x05'+struct.pack('<I',memory+0xe000)+b'\xc2\x04\x00')
    cpu.mem_write(factories,b'\xa1'+struct.pack('<I',memory+0xe004)+b'\xc2\x08\x00')
    cpu.mem_write(memory+0xd100,b'\xa1'+struct.pack('<I',memory+0xe008)+b'\xc3')
    for case, expected in zip(cases,native):
        cost,speed,country_mult,type_mult,power,drain,low,high,penalty_mult,multiple_mult,count,wall,wall_mult=case
        word(kind+cost_offset,cost)
        double(rules+speed_offset,speed)
        single(memory+0xe000,country_mult)
        single(kind+0x608,type_mult)
        word(house+power_offset,power)
        word(house+power_offset+4,drain)
        for offset,value in ((minimum,low),(maximum,high),(penalty,penalty_mult),(multiple,multiple_mult)):
            single(rules+offset,value)
        double(rules+wall_scale,wall_mult)
        cpu.mem_write(kind+wall_offset,bytes([wall]))
        word(memory+0xe008,6 if wall else 1)
        word(memory+0xe004,count)
        word(stack,stop)
        cpu.reg_write(UC_X86_REG_ECX,obj)
        cpu.reg_write(UC_X86_REG_ESP,stack)
        cpu.reg_write(UC_X86_REG_FPCW,0xe7f)
        cpu.emu_start(entry,stop,count=10000)
        actual = struct.unpack('<i',struct.pack('<I',cpu.reg_read(UC_X86_REG_EAX)))[0]
        assert actual == expected,(name,case,actual,expected)
    print(f"{name}: {len(cases)} build-time cases match original x86")
