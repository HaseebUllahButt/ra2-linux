#!/usr/bin/env python3
"""Check base type dispatch with one supplied AMCV UnitType.

Original 00652270 dispatches the original UnitType reader through vtable +64.
Other type registries are supplied empty. The 30 post-dispatch tail calls are
supplied services. This is not a complete rules load or live Battle setup.
"""
import os
import runpy
import struct
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP

if len(sys.argv) not in (5,6) or (len(sys.argv)==6 and sys.argv[5]!='--scanner'):
    raise SystemExit('usage: original_unit_dispatch_check.py game.exe unit_flags_case real_map_case game_dir [--scanner]')
real_scanner=len(sys.argv)==6
sys.argv=[sys.argv[0],*sys.argv[1:5],'--connected-real-scanner' if real_scanner else '--connected-real-format']
loaded=runpy.run_path(os.path.join(os.path.dirname(__file__),'original_unit_reader_205_check.py'))
g=loaded['trace_code'].__globals__
m=g['m']
unit_dispatch=[]
tail_calls=[]

def trace(cpu,address,size,user):
    dispatch_sites=(0x65228d,0x6522b0,0x6522d7,0x6522fa,0x65231d,0x652340,
                    0x652363,0x652386,0x6523a9,0x6523cc,0x6523ec,0x65240f,
                    0x652435,0x652459,0x65247c,0x65249f,0x6524c2,0x6524e5)
    if address in dispatch_sites: unit_dispatch.append(address)
    if address==0x652340:
        assert cpu.reg_read(UC_X86_REG_ECX)==m.TYPE
        assert m.read(cpu.reg_read(UC_X86_REG_ESP))==0x100b000
        assert m.read(m.read(m.TYPE)+0x64)==0x70ab40

def supplied_tail():
    index=len(tail_calls)
    assert index<30 and m.cpu.reg_read(UC_X86_REG_ECX)==0xa403b0+index*0x20
    assert m.argument(0)==0x100b000
    tail_calls.append(index)
    m.cpu.reg_write(UC_X86_REG_EAX,1)

m.cpu.hook_add(UC_HOOK_CODE,trace)
m.bind(0x594e30,4,supplied_tail)
for count in (0xa35d18,0xa40348,0x8661f0,0xa35ce8,0xa3d288,0xa35d58,
              0xa40360,0x839ba0,0xa35d00,0x839b10,0xa40330,0xa40bd8,
              0xa35df8,0xa35e10,0xa35de0,0xa40af0):
    m.word(count,0)
m.word(0xa35d4c,0x14c2000); m.word(0x14c2000,m.TYPE); m.word(0xa35d58,1)
m.word(g['bool_ini']+0x24,1); m.word(g['section']+0x3c,0); m.word(g['art_section']+0x3c,0)
m.word(0x14c1000+0x18,0)
m.cpu.mem_write(0x50c9b0,g['section_prologue']); m.callbacks.pop(0x50c9b0,None)
m.cpu.ctl_remove_cache(0x50c9b0,0x50c9b3)
g['mode']=3
g['base']['parent_result'][0]=1
m.cpu.mem_write(m.TYPE,g['constructor'])
assert m.cpu.mem_read(m.TYPE+0x205,1)==b'\x01'
m.cpu.mem_write(m.TYPE+0x24,b'AMCV\0'); m.cpu.mem_write(m.TYPE+0x1c8,b'AMCV\0')
m.cpu.mem_write(m.TYPE+0x3d,b'\0'); m.cpu.mem_write(m.TYPE+0x64,b'AMCV\0')
g['calls'].update(parent=0,pair=0,format=0,compare=0)
for name in ('chain_calls','writes','section_entries','section_returns'):
    g[name].clear()
g['section_pending'].clear()
for name in ('token_sources','token_returns','token_calls','token_cursor_checks',
             'token_checks','thread_requests','vhpscan_tokens','copy_results',
             'format_entries','format_results','color_entries','color_results',
             'color_scan_results','armor_returns','scan_results'):
    g[name].clear()
for name in ('token_pending','copy_pending','format_pending','color_pending','scan_pending'):
    g[name].clear()
g['vhpscan_in_reader'][0]=False
m.cpu.reg_write(UC_X86_REG_ECX,0); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100b000))
g['active'][0]=True
m.cpu.emu_start(0x652270,0x103e000,count=1000000)
g['active'][0]=False
assert m.cpu.reg_read(UC_X86_REG_EIP)==0x103e000 and m.cpu.reg_read(UC_X86_REG_EAX)&255==1
assert unit_dispatch==[0x652340] and tail_calls==list(range(30))
assert g['chain_calls']==[0x70ab40,0x6daf60,0x5d7810,0x4107e0]
assert g['section_entries']==['AMCV'] and g['section_returns']==[g['section']] and not g['section_pending']
assert len(g['token_checks'])==len(g['token_calls'])==29 and not g['token_pending']
assert len(g['copy_results'])==96 and not g['copy_pending']
assert len(g['format_results'])==8 and not g['format_pending']
if real_scanner: assert len(g['scan_results'])==5 and not g['scan_pending']
assert g['color_results']==[b'\0\0\0',b'\x80\x80\x80'] and not g['color_pending']
assert g['armor_returns']==[5]
assert not g['writes'] and m.cpu.mem_read(m.TYPE+0x205,1)==b'\x01'
assert m.signed(m.TYPE+0x5a4)==1 and m.signed(m.TYPE+0x500)==0 and m.signed(m.TYPE+0x9c)==5
assert struct.unpack('<d',bytes(m.cpu.mem_read(m.TYPE+0x340,8)))[0]==3.5
print('base x86 dispatcher reaches one UnitType and 30 supplied tail calls')
