#!/usr/bin/env python3
"""Check the base startup slice that initializes numeric scanning.

Execution starts at 00785B45 and stops at WinMain 0068E280. Earlier startup,
two initializer-list calls, two floating environment services, command-line
setup, and two Windows services are supplied. Their full effects are not proved.
"""
import struct
import sys

from original_image import load_original
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBP,UC_X86_REG_EIP,UC_X86_REG_ESI,UC_X86_REG_ESP

if len(sys.argv)!=2:
    raise SystemExit('usage: original_runtime_init_check.py game.exe')

def run(null_callback):
    cpu=load_original(sys.argv[1])
    cpu.mem_map(0x1000000,0x10000)
    def word(address): return struct.unpack('<I',bytes(cpu.mem_read(address,4)))[0]
    def put(address,value): cpu.mem_write(address,struct.pack('<I',value))
    def argument(index): return word(cpu.reg_read(UC_X86_REG_ESP)+4+index*4)
    assert word(0x82ebf8)==0x782146 and word(0x82f168)==0x7905e2
    if null_callback: put(0x82ebf8,0)
    # Supplied calls return with the original cdecl or one-argument stdcall ABI.
    for address in (0x786d8f,0x786d3f,0x784fd3,0x78e176): cpu.mem_write(address,b'\xc3')
    cpu.mem_write(0x100d000,b'\xc2\x04\x00'); cpu.mem_write(0x100d010,b'\xc2\x04\x00')
    put(0x79a254,0x100d000); put(0x79a230,0x100d010)
    calls=[]; lists=[]
    def trace(machine,address,size,user):
        watched=(0x785b45,0x784eaf,0x782146,0x78215e,0x786d8f,0x786d3f,
                 0x784fd3,0x100d000,0x78e176,0x100d010,0x68e280)
        if address in watched: calls.append(address)
        if address==0x786d8f: machine.reg_write(UC_X86_REG_EAX,0)
        if address==0x784fd3: lists.append((argument(0),argument(1)))
        if address==0x100d000: assert argument(0)==0x100f000-0x5c
        if address==0x78e176: machine.reg_write(UC_X86_REG_EAX,0x100b000)
        if address==0x100d010:
            assert argument(0)==0
            machine.reg_write(UC_X86_REG_EAX,0x1234)
        if address==0x68e280:
            assert [argument(i) for i in range(4)]==[0x1234,0,0x100b000,10]
            machine.emu_stop()
    cpu.hook_add(UC_HOOK_CODE,trace)
    cpu.reg_write(UC_X86_REG_EBP,0x100f000); cpu.reg_write(UC_X86_REG_ESP,0x100e000)
    cpu.reg_write(UC_X86_REG_ESI,0)
    cpu.emu_start(0x785b45,0x100c000,count=100000)
    assert cpu.reg_read(UC_X86_REG_EIP)==0x68e280
    prefix=[0x785b45,0x784eaf]
    if not null_callback: prefix += [0x782146,0x78215e,0x786d8f,0x786d3f]
    assert calls==prefix+[0x784fd3,0x784fd3,0x100d000,0x78e176,0x100d010,0x68e280]
    assert lists==[(0x7cc870,0x7cc884),(0x7c9000,0x7cc86c)]
    expected=0x7905e2 if null_callback else 0x786e78
    assert word(0x82f168)==expected
    if not null_callback:
        assert [word(address) for address in (0x82f160,0x82f164,0x82f168,0x82f16c,0x82f170,0x82f174)]==[0x787168,0x786e12,0x786e78,0x786db8,0x786e60,0x787168]

run(False)
run(True)
print('base x86 startup slice initializes scanning before WinMain; null callback skips it')
