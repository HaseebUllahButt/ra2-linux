#!/usr/bin/env python3
"""Check Type +205 across the original base ObjectType reader body.

The full 004107E0 and 005D7810 bodies run after the original constructor.
The 005D7630 theater-art body runs when selected. The installed AMCV boolean
values and other INI, color, art loading, and string services are supplied.
Later type reads are omitted.
"""
import os
import runpy
import struct
import sys
from itertools import product
from unicorn import UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDI,UC_X86_REG_EIP,UC_X86_REG_ESP

if len(sys.argv) not in (3,5):
    raise SystemExit('usage: original_object_reader_205_check.py game.exe unit_flags_case [real_map_case game_dir]')
executable,native=sys.argv[1:3]
installed={}
if len(sys.argv)==5:
    import subprocess
    lines=subprocess.check_output([sys.argv[3],sys.argv[4],'--amcv-object-bools'],text=True).splitlines()
    installed={key.casefold():value for key,value in (line.split('\t',1) for line in lines)}
    assert len(installed)==15
sys.argv=[sys.argv[0],executable,native]
base=runpy.run_path(os.path.join(os.path.dirname(__file__),'original_type_ctor_fields_check.py'))
m=base['m']
constructor_bytes=bytes(m.cpu.mem_read(m.TYPE,0xc00))
assert constructor_bytes[0x205]==1 and constructor_bytes[0x206]==0
seen=[]
active=[False]
bool_mode=[0]
parent_result=[1]
theater_calls=[0]
voxel_result=[0]
service_calls={'section':0,'boolean':0,'integer':0,'string':0,'color':0,'format':0,'load':0}

def note_write(cpu,access,address,size,value,user):
    if active[0] and address<=m.TYPE+0x205<address+size:
        seen.append((address,size,value))

def boolean():
    service_calls['boolean']+=1
    default=m.argument(2)&255
    key=bytes(m.cpu.mem_read(m.argument(1),64)).split(b'\0',1)[0].decode('ascii')
    if bool_mode[0]==3:
        section=bytes(m.cpu.mem_read(m.argument(0),64)).split(b'\0',1)[0].decode('ascii')
        assert section.casefold()=='amcv'
        value=installed.get(key.casefold(),'<absent>')
        first=value[:1].lower()
        result=default if value=='<absent>' or first not in ('0','1','f','n','t','y') else int(first in ('1','t','y'))
    else:
        result=default if bool_mode[0]==0 else int(bool_mode[0]==1)
    if key=='Voxel': voxel_result[0]=result
    m.cpu.reg_write(UC_X86_REG_EAX,result)

def section_lookup():
    service_calls['section']+=1
    assert bytes(m.cpu.mem_read(m.argument(0),5))==b'AMCV\0'
    m.cpu.reg_write(UC_X86_REG_EAX,parent_result[0])

def copy_string():
    target,source,count=m.argument(0),m.argument(1),m.argument(2)
    assert count<=1024
    raw=bytes(m.cpu.mem_read(source,count)).split(b'\0',1)[0][:count]
    m.cpu.mem_write(target,raw+bytes(count-len(raw)))
    m.cpu.reg_write(UC_X86_REG_EAX,target)

def integer():
    service_calls['integer']+=1
    m.cpu.reg_write(UC_X86_REG_EAX,m.argument(2))

def string_reader():
    service_calls['string']+=1
    source,target,capacity=m.argument(2),m.argument(3),m.argument(4)
    assert capacity>0 and capacity<=1024
    raw=bytes(m.cpu.mem_read(source,capacity)).split(b'\0',1)[0] if source else b''
    raw=raw[:capacity-1]
    m.cpu.mem_write(target,raw+b'\0')
    m.cpu.reg_write(UC_X86_REG_EAX,len(raw))

def color_reader():
    service_calls['color']+=1
    target,source=m.argument(0),m.argument(3)
    m.cpu.mem_write(target,bytes(m.cpu.mem_read(source,3)))
    m.cpu.reg_write(UC_X86_REG_EAX,target)

def trace_code(cpu,address,size,user):
    if active[0] and address==0x5d7630: theater_calls[0]+=1

def format_name():
    service_calls['format']+=1
    m.cpu.mem_write(m.argument(0),b'ALPHA\0')
    m.cpu.reg_write(UC_X86_REG_EAX,5)

def load_art():
    service_calls['load']+=1
    m.cpu.reg_write(UC_X86_REG_EAX,0)

m.cpu.hook_add(UC_HOOK_MEM_WRITE,note_write)
m.cpu.hook_add(UC_HOOK_CODE,trace_code)
m.bind(0x50c9b0,4,section_lookup); m.bind(0x7823d0,0,copy_string)
m.bind(0x50f410,12,boolean); m.bind(0x50d7f0,12,integer)
m.bind(0x50e9a0,20,string_reader); m.bind(0x46a8b0,12,integer)
m.bind(0x46a010,16,color_reader)
m.bind(0x7831f0,0,format_name); m.bind(0x5956c0,0,load_art)
m.word(0xa3d290,0x1018000); m.word(0x1018000+0x838,0)
for initial,mode,alpha in product((0,1),(0,1,2,3) if installed else (0,1,2),(False,True)):
        bool_mode[0]=mode; parent_result[0]=1; theater_calls[0]=0
        service_calls.update(section=0,boolean=0,integer=0,string=0,color=0,format=0,load=0)
        m.cpu.mem_write(m.TYPE,constructor_bytes)
        m.cpu.mem_write(m.TYPE+0x205,bytes([initial]))
        m.cpu.mem_write(m.TYPE+0x24,b'AMCV\0'); m.cpu.mem_write(m.TYPE+0x3d,b'\0')
        m.cpu.mem_write(m.TYPE+0x64,b'AMCV\0')
        m.cpu.mem_write(m.TYPE+0x1c8,b'AMCV\0')
        m.cpu.mem_write(m.TYPE+0x1e3,b'ALPHA\0' if alpha else b'\0')
        m.cpu.mem_write(m.TYPE+0x206,b'\0')
        m.cpu.reg_write(UC_X86_REG_ECX,m.TYPE); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
        m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100b000))
        seen.clear(); active[0]=True
        m.cpu.emu_start(0x5d7810,0x103e000,count=100000)
        active[0]=False
        assert m.cpu.reg_read(UC_X86_REG_EIP)==0x103e000 and m.cpu.reg_read(UC_X86_REG_EAX)&255==1
        assert not seen and m.cpu.mem_read(m.TYPE+0x205,1)==bytes([initial])
        assert theater_calls[0]==(0 if voxel_result[0] else 1)
        assert service_calls=={'section':1,'boolean':15,'integer':3,'string':6,'color':2,
                              'format':int(alpha)+theater_calls[0],
                              'load':int(alpha)+theater_calls[0]}
parent_result[0]=0
m.cpu.mem_write(m.TYPE+0x205,b'\x01')
m.cpu.reg_write(UC_X86_REG_ECX,m.TYPE); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100b000))
seen.clear(); active[0]=True
m.cpu.emu_start(0x5d7810,0x103e000,count=100000)
active[0]=False
assert m.cpu.reg_read(UC_X86_REG_EIP)==0x103e000 and m.cpu.reg_read(UC_X86_REG_EAX)&255==0
assert not seen and m.cpu.mem_read(m.TYPE+0x205,1)==b'\x01'
print('base x86 ObjectType reader preserves supplied Type +205 in %d checked cases'%(17 if installed else 13))
