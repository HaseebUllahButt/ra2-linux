#!/usr/bin/env python3
"""Check base TileType constructor and absent-key reader guards.

Original 00528EE0, 0052AB8F, and 0052ABE7 instructions execute. The parent
constructor, registry capacity, formatting, and absent-key lookup are supplied.
This does not prove tile replacement or complete Cell setup.
"""
import struct
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBP,UC_X86_REG_ECX,UC_X86_REG_ESP
if len(sys.argv)!=2:
    raise SystemExit('usage: original_tile_type_check.py game.exe')
sys.argv=[sys.argv[0],sys.argv[1],'--base']
import original_move_trace as m
cpu=m.cpu
cpu.mem_write(m.TYPE,b'\xa5'*0x400)
m.bind(0x5d57f0,4,m.ret(0))
m.word(0xac0cb8,0);m.word(0xac0cc0,0);m.word(0xac0cc4,0)
cpu.reg_write(UC_X86_REG_ECX,m.TYPE)
cpu.reg_write(UC_X86_REG_ESP,0x103f000)
cpu.mem_write(0x103f000,struct.pack('<IIIIII',0x103e000,131,0,0,0,1))
cpu.emu_start(0x528ee0,0x103e000,count=100000)
assert m.read(m.TYPE+0x298)==0xffffffff and m.read(m.TYPE+0x2a4)==0xffffffff
assert cpu.mem_read(m.TYPE+0x2b1,1)==b'\0'
# ShadowCaster defaults to false. The base reader skips its assignment.
stack=0x103e000
cpu.mem_write(stack,b'\0'*0x1000)
cpu.reg_write(UC_X86_REG_ESP,stack)
cpu.reg_write(UC_X86_REG_EBP,m.TYPE)
cpu.reg_write(UC_X86_REG_EAX,0)
cpu.emu_start(0x52ab8f,0x52abb7,count=10000)
assert cpu.mem_read(m.TYPE+0x2b1,1)==b'\0'
# Test both a missing set section and a present section with no TileNNAnim.
m.bind(0x7820f4,0,m.ret(0))
m.bind(0x50e9a0,20,m.ret(0))
for section_present in (0,1):
    cpu.mem_write(stack,b'\0'*0x1000)
    cpu.mem_write(stack+0x1d,bytes((section_present,)))
    cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.reg_write(UC_X86_REG_EBP,m.TYPE)
    cpu.emu_start(0x52abe7,0x52ad54,count=10000)
    assert m.read(m.TYPE+0x298)==0xffffffff and m.read(m.TYPE+0x2a4)==0xffffffff
print('base x86 TileType defaults and absent animation/shadow guards match')
