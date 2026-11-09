#!/usr/bin/env python3
"""Check the armed ordinary vehicle idle decision with resolved ability state.
Requires Unicorn. Usage: original_idle_check.py /path/to/executable [--base]
This checks the decision slice. It does not substitute full mission callbacks.
"""
import struct
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP
from original_image import load_original
base='--base' in sys.argv
cpu=load_original(sys.argv[1]); cpu.mem_map(0x1000000,0x40000)
UNIT,HOUSE,TYPE,RULES=0x1000000,0x1001000,0x1002000,0x1004000

def word(address,value): cpu.mem_write(address,struct.pack('<I',value&0xffffffff))
word(UNIT+(0x1b4 if base else 0x21c),HOUSE)
word(UNIT+(0x5ac if base else 0x6c4),TYPE)
word(0x839848 if base else 0x8871e0,RULES)
word(RULES+(0x10a8 if base else 0x1440),2)
ability_address=0x6d7910 if base else 0x70d0d0
cpu.mem_write(ability_address,b'\xc2\x04\x00')
ability=False

def resolved_ability(cpu,address,size,user): cpu.reg_write(UC_X86_REG_EAX,int(ability))
cpu.hook_add(UC_HOOK_CODE,resolved_ability,begin=ability_address,end=ability_address)
h=2166136261; count=0
for iq in (0,1,2,3,5):
    for default in (False,True):
        for ability in (False,True):
            for mask in range(16):
                word(HOUSE+(0x194 if base else 0x24c),iq)
                cpu.mem_write(TYPE+(0xb00 if base else 0xd39),bytes([default]))
                for bit,offset in enumerate((0x4c4 if base else 0x5d4,0x2dc,0x2d8,0x2e4)):
                    word(UNIT+offset,bool(mask&(1<<bit)))
                cpu.reg_write(UC_X86_REG_ESI,UNIT); cpu.reg_write(UC_X86_REG_ESP,0x103f000)
                cpu.emu_start(0x6fd7f2 if base else 0x738b67,0x6fd8fa if base else 0x738c9d,count=10000)
                mission=cpu.reg_read(UC_X86_REG_EDI if base else UC_X86_REG_EBP)
                assert mission in (5,11)
                h=((h^mission)*16777619)&0xffffffff; count+=1
expected=0xe4ba2365 if base else 0x2a52cf05
assert count==320 and h==expected
print('original armed idle cases',count,'hash',f'{h:08x}')
