#!/usr/bin/env python3
"""Check base UnitType IsTrain and Crusher reads against native outputs.

The original constructor prefix, INI hashes, entry searches, boolean reader,
and field writes execute. The INI records and parent helpers are supplied.
Other type-loading phases and live Unit state are not reconstructed.
"""
import os
import runpy
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP

if len(sys.argv) not in (4,5):
    raise SystemExit('usage: original_unit_flags_check.py game.exe real_map_case game_dir [unit_flags_case]')
executable,fixture,game=sys.argv[1:4]
native_case=sys.argv[4] if len(sys.argv)==5 else None
installed=subprocess.check_output([fixture,game,'--amcv-flags-info'],text=True).split()
assert installed==['<absent>','yes']
sys.argv=[sys.argv[0],executable,fixture,game]
base=runpy.run_path(os.path.join(os.path.dirname(__file__),'original_speed_type_check.py'))
m=base['m']
hashes=base['original_hashes'](('AMCV','Crusher','IsTrain'))
ini,section_cache,section,entry,node,value=(base[name] for name in
    ('ini','section_cache','section','entry','node','value'))
train_node,train_value=0x100d800,0x100d900

def signed32(number):
    return number if number<0x80000000 else number-0x100000000

cases=((None,installed[1]),('yes','no'),('true','false'),('1','0'),
       ('no','yes'),('false','true'),('0','1'),('YeS','nO'),('TrUe','FaLsE'),(None,None))
results=[]
for train,crusher in cases:
    records=[]
    if crusher is not None:
        records.append((hashes['Crusher'],node))
        m.cpu.mem_write(value,crusher.encode('ascii')+b'\0')
    if train is not None:
        records.append((hashes['IsTrain'],train_node))
        m.word(train_node+0x10,train_value)
        m.cpu.mem_write(train_value,train.encode('ascii')+b'\0')
    records.sort(key=lambda item:signed32(item[0]))
    for i,(digest,pointer) in enumerate(records):
        m.word(entry+i*8,digest); m.word(entry+i*8+4,pointer)
    m.word(section_cache,hashes['AMCV']); m.word(section_cache+4,section)
    m.word(section+0x2c,entry); m.word(section+0x30,len(records)); m.word(section+0x38,1)
    m.word(section+0x3c,entry+8*next((i for i,item in enumerate(records) if item[0]==hashes['Crusher']),0) if records else 0)
    m.word(node+0x10,value)
    m.cpu.mem_write(m.TYPE,b'\xa5'*0xc00)
    m.cpu.reg_write(UC_X86_REG_ECX,m.TYPE); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100a000))
    m.cpu.emu_start(0x70a600,0x6da3f4,count=100000)
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x6da3f4
    assert m.cpu.mem_read(m.TYPE+0xaa0,1)==b'\0' and m.cpu.mem_read(m.TYPE+0xaf3,1)==b'\0'
    for start,stop in ((0x6db03a,0x6db054),(0x6dd104,0x6dd11e)):
        m.cpu.reg_write(UC_X86_REG_EBP,m.TYPE); m.cpu.reg_write(UC_X86_REG_ESI,ini)
        m.cpu.reg_write(UC_X86_REG_EBX,0x100a000); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
        m.cpu.emu_start(start,stop,count=10000)
        assert m.cpu.reg_read(UC_X86_REG_EIP)==stop
    values=(m.cpu.mem_read(m.TYPE+0xaa0,1)[0],m.cpu.mem_read(m.TYPE+0xaf3,1)[0])
    expected=(int(train is not None and train.lower() in ('yes','true','1')),
              int(crusher is not None and crusher.lower() in ('yes','true','1')))
    assert values==expected
    results.append(values)
if native_case:
    native=[tuple(map(int,line.split())) for line in subprocess.check_output([native_case,'--vectors'],text=True).splitlines()]
    assert native==results
print('base x86 UnitType IsTrain and Crusher defaults and supported values pass')
