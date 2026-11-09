#!/usr/bin/env python3
"""Check original foundation bounds and optional Rules Bib row in both editions."""
import struct
import sys
from original_image import load_original
from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EAX
cpu=load_original(sys.argv[1]); base='--base' in sys.argv
cpu.mem_map(0x1000000,0x10000)
OBJECT,STACK,STOP=0x1000000,0x100f000,0x100e000
bounds=((1,1),(2,1),(1,2),(2,2),(2,3),(3,2),(3,3),(3,5),(4,2),(3,3),(1,3),(3,1),(4,3),(1,4),(1,5),(2,6),(2,5),(5,3),(4,4),(3,4),(6,4),(0,0))
cases=0; h=2166136261
for index,(width,height) in enumerate(bounds):
 for bib in (0,1):
  for include in (0,1):
   cpu.mem_write(OBJECT+(0xc78 if base else 0xef0),struct.pack('<I',index))
   cpu.mem_write(OBJECT+(0x1214 if base else 0x1570),bytes([bib]))
   values=[]
   for address in ((0x458930,0x458940) if base else (0x45ec90,0x45eca0)):
    cpu.reg_write(UC_X86_REG_ECX,OBJECT); cpu.reg_write(UC_X86_REG_ESP,STACK)
    cpu.mem_write(STACK,struct.pack('<II',STOP,include))
    cpu.emu_start(address,STOP,count=1000); values.append(cpu.reg_read(UC_X86_REG_EAX))
   assert tuple(values)==(width,height+(bib and include)),(index,bib,include,values)
   for value in values: h=((h^value)*16777619)&0xffffffff
   cases+=1
print('original foundation bounds cases=%d hash=%08X'%(cases,h))
