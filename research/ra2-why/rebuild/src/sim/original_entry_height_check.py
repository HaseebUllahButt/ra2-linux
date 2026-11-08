#!/usr/bin/env python3
"""Run original Unit height entry gate with resolved Cell records. Requires Unicorn."""
import struct
import sys
from original_image import load_original
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
cpu=load_original(sys.argv[1]); base='--base' in sys.argv
cpu.mem_map(0x1000000,0x10000)
SOURCE,DESTINATION,HEIGHT,ABOVE,STACK,STOP=0x1000000,0x1001000,0x1002000,0x1002010,0x100f000,0x100e000
h=2166136261; cases=0
for source_level in (-128,-4,-1,0,1,4,127):
 for destination_level in (-128,-4,-1,0,1,4,127):
  for layer in (-132,-128,-4,-1,0,1,4,127,131):
   for source_ramp in (0,1):
    for destination_ramp in (0,1):
     for source_flags in (0,0x100,0x200,0x300):
      for destination_flags in (0,0x100,0x200,0x300):
       for direction in (-1,0):
        for above in (0,1):
         for cell,level,ramp,flags in ((SOURCE,source_level,source_ramp,source_flags),(DESTINATION,destination_level,destination_ramp,destination_flags)):
          cpu.mem_write(cell+0x11b,struct.pack('<bB',level,ramp)); cpu.mem_write(cell+0x140,struct.pack('<I',flags))
         cpu.mem_write(HEIGHT,struct.pack('<i',layer)); cpu.mem_write(ABOVE,bytes([above]))
         cpu.reg_write(UC_X86_REG_ESP,STACK); cpu.reg_write(UC_X86_REG_ECX,0)
         cpu.mem_write(STACK,struct.pack('<6I',STOP,DESTINATION,direction&0xffffffff,HEIGHT,ABOVE,SOURCE))
         cpu.emu_start(0x4c8730 if base else 0x4d9c60,STOP,count=1000)
         assert cpu.reg_read(UC_X86_REG_EIP)==STOP,'height-gate instruction bound exceeded'
         values=(cpu.reg_read(UC_X86_REG_EAX),struct.unpack('<I',cpu.mem_read(HEIGHT,4))[0],cpu.mem_read(ABOVE,1)[0])
         assert values[0] in (0,7),values
         for value in values: h=((h^value)*16777619)&0xffffffff
         cases+=1
print('original height entry cases=%d hash=%08X'%(cases,h))
