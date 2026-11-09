#!/usr/bin/env python3
"""Check the base Rules divisor constructor and reader instruction slices.

The installed map and rules supply the resolved value. The INI integer parser
is supplied. Other Rules construction and loading phases are not executed.
"""
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDI,UC_X86_REG_EIP,UC_X86_REG_ESI,UC_X86_REG_ESP

if len(sys.argv)!=4:
    raise SystemExit('usage: original_sight_rule_check.py game.exe real_map_case game_dir')
executable,fixture,game=sys.argv[1:]
installed=int(subprocess.check_output([fixture,game,'--sight-info'],text=True).strip())
assert installed==2000
sys.argv=[sys.argv[0],executable,'--base']
import original_move_trace as m

def string(address):
    return bytes(m.cpu.mem_read(address,64)).split(b'\0',1)[0].decode('ascii')

selected=[installed]
def integer_reader():
    assert string(m.argument(0))=='General'
    assert string(m.argument(1))=='LeptonsPerSightIncrease'
    assert m.argument(2)==50
    m.cpu.reg_write(UC_X86_REG_EAX,selected[0])

m.bind(0x50d7f0,12,integer_reader)
for value in (installed,50):
    selected[0]=value
    m.word(m.TYPE+0x1324,0xdeadbeef)
    m.cpu.reg_write(UC_X86_REG_ESI,m.TYPE)
    m.cpu.emu_start(0x6425cd,0x6425d7,count=100)
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x6425d7 and m.read(m.TYPE+0x1324)==50
    m.cpu.reg_write(UC_X86_REG_ESI,m.TYPE); m.cpu.reg_write(UC_X86_REG_EDI,0x100b000)
    m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.mem_write(0x103f000,struct.pack('<I',0x103e000))
    m.cpu.emu_start(0x64a8da,0x64a8f9,count=10000)
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x64a8f9 and m.read(m.TYPE+0x1324)==value
print('base x86 Rules sight divisor defaults to 50 and accepts the installed value 2000')
