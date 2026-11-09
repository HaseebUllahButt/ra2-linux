#!/usr/bin/env python3
"""Check Type +205 across the original base UnitType reader body.

The full 0070AB40 body runs with restored constructor state. In connected mode,
the ObjectType and TechnoType reader bodies also execute, including one case
with installed AMCV boolean values. In connected-real-bool mode, the original
boolean reader uses a separately supplied cache of installed AMCV records.
Other INI, formatting, enumeration, allocation, and asset services are supplied.
Mode rules and later loading remain omitted.
"""
import os
import runpy
import struct
import subprocess
import sys
from itertools import product
from unicorn import UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP

if len(sys.argv) not in (5,6) or (len(sys.argv)==6 and sys.argv[5] not in ('--connected','--connected-real-bool')):
    raise SystemExit('usage: original_unit_reader_205_check.py game.exe unit_flags_case real_map_case game_dir [--connected|--connected-real-bool]')
executable,native,fixture,game=sys.argv[1:5]
connected=len(sys.argv)==6
real_bool=connected and sys.argv[5]=='--connected-real-bool'
sys.argv=[sys.argv[0],executable,native,fixture,game]
if connected:
    techno=runpy.run_path(os.path.join(os.path.dirname(__file__),'original_techno_reader_205_check.py'))
    base=techno['base']
    lines=subprocess.check_output([fixture,game,'--amcv-all-rules'],text=True).splitlines()
    base['installed'].update({key.casefold():value for key,value in (line.split('\t',1) for line in lines)})
    raw_rules=dict(line.split('\t',1) for line in lines)
else:
    base=runpy.run_path(os.path.join(os.path.dirname(__file__),'original_object_reader_205_check.py'))
m=base['m']
constructor=base['constructor_bytes']
active=[False]
writes=[]
calls={'parent':0,'pair':0,'format':0,'compare':0}
chain_calls=[]
parent_ok=[True]
bool_entries=[]

def trace_write(cpu,access,address,size,value,user):
    if active[0] and address<=m.TYPE+0x205<address+size:
        writes.append((address,size,value))

def parent_reader():
    calls['parent']+=1
    m.cpu.reg_write(UC_X86_REG_EAX,int(parent_ok[0]))

def trace_code(cpu,address,size,user):
    if active[0] and connected:
        if address in (0x70ab40,0x6daf60,0x5d7810,0x4107e0): chain_calls.append(address)
        if address==0x6daf60: calls['parent']+=1
        if real_bool and address==0x50f410:
            section=bytes(cpu.mem_read(m.argument(0),64)).split(b'\0',1)[0].decode('ascii')
            assert section.casefold()=='amcv'
            bool_entries.append(address)
            # Supply records only to the boolean reader. Other INI services
            # keep their separately supplied context.
            if mode==3: cpu.reg_write(UC_X86_REG_ECX,bool_ini)

def pair_reader():
    calls['pair']+=1
    target,source=m.argument(0),m.argument(3)
    m.cpu.mem_write(target,bytes(m.cpu.mem_read(source,12)))
    m.cpu.reg_write(UC_X86_REG_EAX,target)

def format_key():
    calls['format']+=1
    target=m.argument(0); m.cpu.mem_write(target,b'Key\0')
    m.cpu.reg_write(UC_X86_REG_EAX,3)

def compare_names():
    calls['compare']+=1
    left=bytes(m.cpu.mem_read(m.argument(0),32)).split(b'\0',1)[0].lower()
    right=bytes(m.cpu.mem_read(m.argument(1),32)).split(b'\0',1)[0].lower()
    m.cpu.reg_write(UC_X86_REG_EAX,int(left!=right))

m.cpu.hook_add(UC_HOOK_MEM_WRITE,trace_write)
m.cpu.hook_add(UC_HOOK_CODE,trace_code)
if not connected: m.bind(0x6daf60,4,parent_reader)
m.bind(0x50fa40,16,pair_reader)
m.bind(0x46c400,12,base['integer']); m.bind(0x46a970,12,base['integer'])
m.bind(0x7820f4,0,format_key); m.bind(0x781f20,0,compare_names)
if real_bool:
    from original_image import load_original
    hash_cpu=load_original(executable); hash_cpu.mem_map(0x1400000,0x10000)
    def original_hash(name):
        raw=name.encode('ascii'); assert len(raw)<128
        hash_cpu.mem_write(0x1401000,bytes(16)); hash_cpu.mem_write(0x1402000,raw+b'\0')
        hash_cpu.mem_write(0x140e000,struct.pack('<III',0x140f000,0x1402000,len(raw)))
        hash_cpu.reg_write(UC_X86_REG_ECX,0x1401000); hash_cpu.reg_write(UC_X86_REG_ESP,0x140e000)
        hash_cpu.emu_start(0x493fc0,0x140f000,count=10000)
        assert hash_cpu.reg_read(UC_X86_REG_EIP)==0x140f000
        return hash_cpu.reg_read(UC_X86_REG_EAX)
    entries=sorted(((original_hash(key),value) for key,value in raw_rules.items()),
                   key=lambda pair:pair[0] if pair[0]<0x80000000 else pair[0]-0x100000000)
    assert len(entries)<1024 and len({key for key,_ in entries})==len(entries)
    m.cpu.mem_map(0x1300000,0x200000)
    section_cache,section,bool_ini,array=0x1301000,0x1302000,0x1303000,0x1304000
    strings=0x1380000
    for index,(digest,value) in enumerate(entries):
        node=0x1320000+index*0x40; encoded=value.encode('utf-8')+b'\0'
        assert strings+len(encoded)<0x1500000
        m.cpu.mem_write(strings,encoded); m.word(node+0x10,strings); strings+=len(encoded)
        m.word(array+8*index,digest); m.word(array+8*index+4,node)
    m.word(section_cache,original_hash('AMCV')); m.word(section_cache+4,section)
    m.word(section+0x2c,array); m.word(section+0x30,len(entries)); m.word(section+0x38,1)
    m.word(bool_ini+0x24,1); m.word(bool_ini+0x30,section_cache)
    boolean_prologue=bytes(hash_cpu.mem_read(0x50f410,3))
for initial,mode in product((0,1),(0,1,2,3) if connected else (0,1,2)):
    m.cpu.mem_write(m.TYPE,constructor)
    m.cpu.mem_write(m.TYPE+0x205,bytes([initial]))
    m.cpu.mem_write(m.TYPE+0x24,b'AMCV\0'); m.cpu.mem_write(m.TYPE+0x1c8,b'AMCV\0')
    m.cpu.mem_write(m.TYPE+0x3d,b'\0'); m.cpu.mem_write(m.TYPE+0x64,b'AMCV\0')
    base['parent_result'][0]=1
    base['bool_mode'][0]=mode
    if real_bool:
        if mode==3:
            m.cpu.mem_write(0x50f410,boolean_prologue); m.callbacks.pop(0x50f410,None)
            m.word(section+0x3c,0)
        else:
            m.word(0x100b000+0x24,0)
            m.bind(0x50f410,12,base['boolean'])
        m.cpu.ctl_remove_cache(0x50f410,0x50f413)
    calls.update(parent=0,pair=0,format=0,compare=0)
    chain_calls.clear()
    bool_entries.clear()
    m.cpu.reg_write(UC_X86_REG_ECX,m.TYPE); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100b000))
    writes.clear(); active[0]=True
    m.cpu.emu_start(0x70ab40,0x103e000,count=1000000)
    active[0]=False
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x103e000 and m.cpu.reg_read(UC_X86_REG_EAX)&255==1
    assert not writes and m.cpu.mem_read(m.TYPE+0x205,1)==bytes([initial])
    if connected and mode==3:
        assert m.cpu.mem_read(m.TYPE+0xaf3,1)==b'\x01' and m.cpu.mem_read(m.TYPE+0xaa0,1)==b'\x00'
        assert m.signed(m.TYPE+0x5a4)==1
        if real_bool: assert len(bool_entries)>100
    assert calls['parent']==1 and calls['pair']>=1 and calls['format']>=6 and calls['compare']>=1
    if connected: assert chain_calls==[0x70ab40,0x6daf60,0x5d7810,0x4107e0]
parent_ok[0]=False
if connected: base['parent_result'][0]=0
m.cpu.mem_write(m.TYPE,constructor); m.cpu.mem_write(m.TYPE+0x205,b'\x01')
m.cpu.mem_write(m.TYPE+0x24,b'AMCV\0')
calls.update(parent=0,pair=0,format=0,compare=0)
chain_calls.clear()
m.cpu.reg_write(UC_X86_REG_ECX,m.TYPE); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100b000))
writes.clear(); active[0]=True
m.cpu.emu_start(0x70ab40,0x103e000,count=1000000)
active[0]=False
assert m.cpu.reg_read(UC_X86_REG_EIP)==0x103e000 and m.cpu.reg_read(UC_X86_REG_EAX)&255==0
assert not writes and m.cpu.mem_read(m.TYPE+0x205,1)==b'\x01'
assert calls=={'parent':1,'pair':0,'format':0,'compare':0}
if connected: assert chain_calls==[0x70ab40,0x6daf60,0x5d7810,0x4107e0]
print('base x86 %sreader chain preserves supplied Type +205 in %d checked cases'%
      ('connected real-boolean ' if real_bool else 'connected ' if connected else 'UnitType ',9 if connected else 7))
