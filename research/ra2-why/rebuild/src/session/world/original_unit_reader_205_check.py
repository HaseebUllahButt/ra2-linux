#!/usr/bin/env python3
"""Check Type +205 across the original base UnitType reader body.

The full 0070AB40 body runs with restored constructor state. In connected mode,
the ObjectType and TechnoType reader bodies also execute, including one case
with installed AMCV boolean values. In connected-real-bool mode, the original
boolean reader uses a separately supplied cache of installed AMCV records.
In connected-real-art mode, original string and boolean readers also use
separate supplied caches for installed AMCV rules and MCV art records.
In connected-real-integer mode, the original integer reader uses both caches.
In connected-real-float mode, the original float reader uses both caches.
The fixture supplies its restricted float scanner and percent search.
Other INI, formatting, enumeration, allocation, and asset services are supplied.
Mode rules and later loading remain omitted.
"""
import os
import runpy
import struct
import subprocess
import sys
from itertools import product
from unicorn import UC_HOOK_CODE,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP

if len(sys.argv) not in (5,6) or (len(sys.argv)==6 and sys.argv[5] not in ('--connected','--connected-real-bool','--connected-real-art','--connected-real-integer','--connected-real-float')):
    raise SystemExit('usage: original_unit_reader_205_check.py game.exe unit_flags_case real_map_case game_dir [--connected|--connected-real-bool|--connected-real-art|--connected-real-integer|--connected-real-float]')
executable,native,fixture,game=sys.argv[1:5]
connected=len(sys.argv)==6
real_bool=connected and sys.argv[5] in ('--connected-real-bool','--connected-real-art','--connected-real-integer','--connected-real-float')
real_art=connected and sys.argv[5] in ('--connected-real-art','--connected-real-integer','--connected-real-float')
real_integer=connected and sys.argv[5] in ('--connected-real-integer','--connected-real-float')
real_float=connected and sys.argv[5]=='--connected-real-float'
sys.argv=[sys.argv[0],executable,native,fixture,game]
if connected:
    techno=runpy.run_path(os.path.join(os.path.dirname(__file__),'original_techno_reader_205_check.py'))
    base=techno['base']
    lines=subprocess.check_output([fixture,game,'--amcv-all-rules'],text=True).splitlines()
    base['installed'].update({key.casefold():value for key,value in (line.split('\t',1) for line in lines)})
    raw_rules=dict(line.split('\t',1) for line in lines)
    if real_art:
        assert raw_rules.get('Image')=='MCV'
        lines=subprocess.check_output([fixture,game,'--mcv-all-art'],text=True).splitlines()
        raw_art=dict(line.split('\t',1) for line in lines)
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
art_entries=[]
art_value_reads=set()
art_value_ranges={}
integer_entries=[]
integer_pending={}
integer_checked=set()
float_entries=[]
float_conversions=[]

def trace_write(cpu,access,address,size,value,user):
    if active[0] and address<=m.TYPE+0x205<address+size:
        writes.append((address,size,value))

def trace_art_value(cpu,access,address,size,value,user):
    if active[0] and real_art and mode==3:
        for key,(start,end) in art_value_ranges.items():
            if address<end and address+size>start: art_value_reads.add(key)

def checked_copy_string():
    source=m.argument(1)
    if active[0] and real_art and mode==3:
        for key,(start,end) in art_value_ranges.items():
            if start<=source<end: art_value_reads.add(key)
    base['copy_string']()

def supplied_scan_float():
    source,format_ptr,target=m.argument(0),m.argument(1),m.argument(2)
    assert bytes(m.cpu.mem_read(format_ptr,3))==b'%f\0'
    text=bytes(m.cpu.mem_read(source,256)).split(b'\0',1)[0].decode('ascii')
    assert text in ('3.5','6','1'), text
    m.cpu.mem_write(target,struct.pack('<f',float(text)))
    m.cpu.reg_write(UC_X86_REG_EAX,1)
    float_conversions.append(text)

def supplied_percent_search():
    source,needle=m.argument(0),m.argument(1)
    raw=bytes(m.cpu.mem_read(source,256)).split(b'\0',1)[0]
    assert needle==0x2525
    assert b'%' not in raw
    m.cpu.reg_write(UC_X86_REG_EAX,0)

def parent_reader():
    calls['parent']+=1
    m.cpu.reg_write(UC_X86_REG_EAX,int(parent_ok[0]))

def trace_code(cpu,address,size,user):
    if active[0] and connected:
        if real_integer and mode==3 and address in integer_pending:
            key,expected=integer_pending.pop(address)
            assert cpu.reg_read(UC_X86_REG_EAX)==expected, key
            integer_checked.add(key)
        if address in (0x70ab40,0x6daf60,0x5d7810,0x4107e0): chain_calls.append(address)
        if address==0x6daf60: calls['parent']+=1
        if real_bool and address==0x50f410:
            section=bytes(cpu.mem_read(m.argument(0),64)).split(b'\0',1)[0].decode('ascii')
            incoming=cpu.reg_read(UC_X86_REG_ECX)
            if real_art and mode==3 and section.casefold()=='mcv':
                assert incoming==0x8397f0
                cpu.reg_write(UC_X86_REG_ECX,art_ini)
                art_entries.append((address,bytes(cpu.mem_read(m.argument(1),64)).split(b'\0',1)[0].decode('ascii')))
                return
            assert section.casefold()=='amcv', (mode,section,hex(incoming))
            if real_art and mode==3: assert incoming==0x100b000
            bool_entries.append(address)
            # Supply records only to the boolean reader. Other INI services
            # keep their separately supplied context.
            if mode==3: cpu.reg_write(UC_X86_REG_ECX,bool_ini)
        if real_art and mode==3 and address==0x50e9a0:
            section=bytes(cpu.mem_read(m.argument(0),64)).split(b'\0',1)[0].decode('ascii')
            incoming=cpu.reg_read(UC_X86_REG_ECX)
            if section.casefold()=='amcv':
                assert incoming==0x100b000
                cpu.reg_write(UC_X86_REG_ECX,bool_ini)
            else:
                assert section.casefold()=='mcv' and incoming==0x8397f0, (section,hex(incoming))
                cpu.reg_write(UC_X86_REG_ECX,art_ini)
                art_entries.append((address,bytes(cpu.mem_read(m.argument(1),64)).split(b'\0',1)[0].decode('ascii')))
        if real_integer and mode==3 and address==0x50d7f0:
            section=bytes(cpu.mem_read(m.argument(0),64)).split(b'\0',1)[0].decode('ascii')
            incoming=cpu.reg_read(UC_X86_REG_ECX)
            key=bytes(cpu.mem_read(m.argument(1),64)).split(b'\0',1)[0].decode('ascii')
            if section.casefold()=='amcv':
                assert incoming==0x100b000
                cpu.reg_write(UC_X86_REG_ECX,bool_ini)
            else:
                assert section.casefold()=='mcv' and incoming==0x8397f0, (section,hex(incoming))
                cpu.reg_write(UC_X86_REG_ECX,art_ini)
            integer_entries.append((section,key))
            expected={'Strength':1000,'Sight':6,'TechLevel':10,'Speed':4,'Cost':3000}
            if (section=='AMCV' and key in expected) or (section=='MCV' and key=='Facings'):
                ret=struct.unpack('<I',bytes(cpu.mem_read(cpu.reg_read(UC_X86_REG_ESP),4)))[0]
                assert ret not in integer_pending
                integer_pending[ret]=(section+':'+key, expected[key] if section=='AMCV' else m.argument(2))
        if real_float and mode==3 and address==0x50e3a0:
            section=bytes(cpu.mem_read(m.argument(0),64)).split(b'\0',1)[0].decode('ascii')
            incoming=cpu.reg_read(UC_X86_REG_ECX)
            key=bytes(cpu.mem_read(m.argument(1),64)).split(b'\0',1)[0].decode('ascii')
            if section.casefold()=='amcv':
                assert incoming==0x100b000
                cpu.reg_write(UC_X86_REG_ECX,bool_ini)
            else:
                assert section.casefold()=='mcv' and incoming==0x8397f0, (section,hex(incoming))
                cpu.reg_write(UC_X86_REG_ECX,art_ini)
            float_entries.append((section,key))

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
if real_art: m.cpu.hook_add(UC_HOOK_MEM_READ,trace_art_value,begin=0x1380000,end=0x14fffff)
if not connected: m.bind(0x6daf60,4,parent_reader)
if real_art: m.bind(0x7823d0,0,checked_copy_string)
if real_float:
    m.bind(0x783730,0,supplied_scan_float)
    m.bind(0x784130,0,supplied_percent_search)
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
    if real_art:
        art_cache,art_section,art_ini,art_array=0x1311000,0x1312000,0x1313000,0x1314000
        assert {key.casefold():value for key,value in raw_art.items()}=={'cameo':'MCVICON','remapable':'yes','voxel':'yes'}
        art_values=sorted(((original_hash(key),key,value) for key,value in raw_art.items()),
                          key=lambda pair:pair[0] if pair[0]<0x80000000 else pair[0]-0x100000000)
        assert len(art_values)<1024 and len({digest for digest,_,_ in art_values})==len(art_values)
        for index,(digest,key,value) in enumerate(art_values):
            node=0x1360000+index*0x40; encoded=value.encode('utf-8')+b'\0'
            assert strings+len(encoded)<0x1500000
            art_value_ranges[key]=(strings,strings+len(encoded))
            m.cpu.mem_write(strings,encoded); m.word(node+0x10,strings); strings+=len(encoded)
            m.word(art_array+8*index,digest); m.word(art_array+8*index+4,node)
        m.word(art_cache,original_hash('MCV')); m.word(art_cache+4,art_section)
        m.word(art_section+0x2c,art_array); m.word(art_section+0x30,len(art_values)); m.word(art_section+0x38,1)
        m.word(art_ini+0x24,1); m.word(art_ini+0x30,art_cache)
        string_prologue=bytes(hash_cpu.mem_read(0x50e9a0,3))
        integer_prologue=bytes(hash_cpu.mem_read(0x50d7f0,3))
        float_prologue=bytes(hash_cpu.mem_read(0x50e3a0,7))
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
            if real_art:
                m.word(art_section+0x3c,0)
                m.cpu.mem_write(0x50e9a0,string_prologue); m.callbacks.pop(0x50e9a0,None)
                m.cpu.ctl_remove_cache(0x50e9a0,0x50e9a3)
            if real_integer:
                m.cpu.mem_write(0x50d7f0,integer_prologue); m.callbacks.pop(0x50d7f0,None)
                m.cpu.ctl_remove_cache(0x50d7f0,0x50d7f3)
            if real_float:
                m.cpu.mem_write(0x50e3a0,float_prologue); m.callbacks.pop(0x50e3a0,None)
                m.cpu.ctl_remove_cache(0x50e3a0,0x50e3a7)
        else:
            m.word(0x100b000+0x24,0)
            m.bind(0x50f410,12,base['boolean'])
            if real_art:
                m.bind(0x50e9a0,20,base['string_reader']); m.cpu.ctl_remove_cache(0x50e9a0,0x50e9a3)
            if real_integer:
                m.bind(0x50d7f0,12,base['integer']); m.cpu.ctl_remove_cache(0x50d7f0,0x50d7f3)
            if real_float:
                m.bind(0x50e3a0,16,lambda:None)
                m.cpu.mem_write(0x50e3a0,b'\xdd\x44\x24\x0c\xc2\x10\x00')
                m.cpu.ctl_remove_cache(0x50e3a0,0x50e3a7)
        m.cpu.ctl_remove_cache(0x50f410,0x50f413)
    calls.update(parent=0,pair=0,format=0,compare=0)
    chain_calls.clear()
    bool_entries.clear()
    art_entries.clear()
    art_value_reads.clear()
    integer_entries.clear()
    integer_pending.clear()
    integer_checked.clear()
    float_entries.clear()
    float_conversions.clear()
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
        if real_art:
            assert {(0x50f410,'Voxel'),(0x50f410,'Remapable'),(0x50e9a0,'Cameo')} <= set(art_entries)
            # Cameo uses the supplied string-copy service. Boolean bytes
            # execute in x86.
            assert {'Voxel','Remapable','Cameo'} <= art_value_reads, art_value_reads
        if real_integer:
            assert {('MCV','Facings'),('AMCV','Strength')} <= set(integer_entries)
            assert integer_checked=={'AMCV:Strength','AMCV:Sight','AMCV:TechLevel','AMCV:Speed','AMCV:Cost','MCV:Facings'} and not integer_pending
        if real_float:
            assert ('AMCV','Weight') in float_entries and set(float_conversions)=={'3.5','6','1'}
            assert struct.unpack('<d',bytes(m.cpu.mem_read(m.TYPE+0x340,8)))[0]==3.5
            assert ('AMCV','AccelerationFactor') in float_entries
            assert bytes(m.cpu.mem_read(m.TYPE+0x2d8,8))==constructor[0x2d8:0x2e0]
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
      ('connected real-float ' if real_float else 'connected real-integer ' if real_integer else 'connected real-art ' if real_art else 'connected real-boolean ' if real_bool else 'connected ' if connected else 'UnitType ',9 if connected else 7))
