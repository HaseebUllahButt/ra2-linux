#!/usr/bin/env python3
"""Check the original Battle setup phase order. Requires Python Unicorn.

Runs the original second Battle-entry prefix and starting-unit driver. The mode
methods and per-house updates are stubbed to expose call order; original RNG
and driver control flow execute. Pass the original game executable.
"""
import pathlib
import struct
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'sim'))
from original_image import load_original
if len(sys.argv)!=2: raise SystemExit('usage: original_phase_check.py game.exe')
exe=sys.argv[1]
base=pathlib.Path(exe).name.lower()=='game.exe'
if base:
    entry,stop=0x65e8c0,0x65e972
    rules_global,rule_threshold=0x839848,0x1138
    human_count,ai_count,suppressed=0xa3fa8c,0xa3d2d4,0xa40d13
    mode_global,mode_vtable,start_slot=0xa3d29c,0x7a66b4,0x6c
    update_start,unit_driver,prestep=0x5b7c00,0x5b7ce0,0x481fb0
    base_slot,unit_slot=0xac,0xb0
    house_global,house_count,unit_count,scenario_global=0xa3229c,0xa322a8,0xa3d2d0,0xa3d290
    cache_offsets=(0x54a0,0x54d0,0x54c0,0x54b0)
    constructor,logger=0x638790,0x4068f0
else:
    entry,stop=0x686890,0x686945
    rules_global,rule_threshold=0x8871e0,0x14d0
    human_count,ai_count,suppressed=0xa8da84,0xa8b274,0xa8ed6b
    mode_global,mode_vtable,start_slot=0xa8b23c,0x7ee184,0x84
    update_start,unit_driver,prestep=0x5d6c70,0x5d6d80,0x48d080
    base_slot,unit_slot=0xc8,0xcc
    house_global,house_count,unit_count,scenario_global=0xa8022c,0xa80238,0xa8b270,0xa8b230
    cache_offsets=(0x55a0,0x55dc,0x55c8,0x55b4)
    constructor,logger=0x65c6d0,0x4068e0

def put(cpu,address,*values):cpu.mem_write(address,struct.pack('<'+'I'*len(values),*(v&0xffffffff for v in values)))
def get(cpu,address):return struct.unpack('<I',cpu.mem_read(address,4))[0]
def track(cpu,address,label,events):
    def hook(cpu,address,size,user):events.append(label)
    cpu.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)

# 00686890 / 0065E8C0 calls the real Battle start virtual before the unit driver.
cpu=load_original(exe);cpu.mem_map(0x1000000,0xa0000)
rules,mode,stack=0x1000000,0x1003000,0x109e000
put(cpu,rules_global,rules);put(cpu,rules+rule_threshold,0)
put(cpu,human_count,0);put(cpu,ai_count,0);cpu.mem_write(suppressed,b'\x00')
put(cpu,mode_global,mode);put(cpu,mode,mode_vtable)
assert get(cpu,mode_vtable+start_slot)==update_start
cpu.mem_write(prestep,b'\xc3');cpu.mem_write(update_start,b'\xc2\x04\x00');cpu.mem_write(unit_driver,b'\xc3')
events=[]
track(cpu,update_start,'second-start-pass',events);track(cpu,unit_driver,'unit-driver',events)
put(cpu,stack,0x109f000);cpu.reg_write(UC_X86_REG_ECX,0);cpu.reg_write(UC_X86_REG_ESP,stack)
cpu.emu_start(entry,stop,count=10000)
assert events==['second-start-pass','unit-driver'],events

# The original driver calls base unit, extra units, then four updates per house.
cpu=load_original(exe);cpu.mem_map(0x1000000,0xa0000)
scenario,mode,houses,house_array,house_types=0x1000000,0x1003000,(0x1010000,0x1030000),0x1050000,0x1051000
stubs,stack,end=0x1080000,0x109e000,0x109f000
put(cpu,scenario_global,scenario);put(cpu,mode,mode_vtable)
put(cpu,house_global,house_array);put(cpu,house_count,2);put(cpu,human_count,0);put(cpu,unit_count,0)
for i,house in enumerate(houses):
    put(cpu,house_array+4*i,house);put(cpu,house+0x34,house_types+0x1000*i)
    cpu.mem_write(house_types+0x1000*i+0x1a6,b'\x00')
    for j,offset in enumerate(cache_offsets):
        vtable=stubs+0x100+j*0x20; stub=stubs+j*0x10
        put(cpu,house+offset,vtable);put(cpu,vtable+0x0c,stub);cpu.mem_write(stub,b'\xc3')
base_method=get(cpu,mode_vtable+base_slot);unit_method=get(cpu,mode_vtable+unit_slot)
cpu.mem_write(base_method,b'\xb0\x01\xc2\x08\x00')
cpu.mem_write(unit_method,b'\xb0\x01\xc2\x08\x00')
cpu.mem_write(logger,b'\xc3')
events=[]
track(cpu,base_method,'base',events);track(cpu,unit_method,'extra',events)
for j in range(4):track(cpu,stubs+j*0x10,'update'+str(j),events)
track(cpu,logger,'debug',events)
put(cpu,stack,end,42);cpu.reg_write(UC_X86_REG_ECX,scenario+0x218);cpu.reg_write(UC_X86_REG_ESP,stack)
cpu.emu_start(constructor,end,count=100000)
put(cpu,stack,end);cpu.reg_write(UC_X86_REG_ECX,mode);cpu.reg_write(UC_X86_REG_ESP,stack)
cpu.emu_start(unit_driver,end,count=100000)
assert cpu.reg_read(UC_X86_REG_EAX)&0xff==1
assert events==(['base','extra','update0','update1','update2','update3']*2+['debug']),events
print(f'Battle phase and per-house update order match original x86: {pathlib.Path(exe).name}')
