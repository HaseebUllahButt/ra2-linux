#!/usr/bin/env python3
"""Check the original base UnitType reader loop for installed vehicles.

The original loop and reader bodies run after the 55-type registration check.
Base RULES.INI and ART.INI records are supplied in synthetic INI storage;
map and mode overlays are omitted. Allocation and GUID parsing are supplied.
The asset loader returns null, one art-name formatter returns a synthetic name,
and runtime, pair, free, and thread-storage services are supplied. These inputs
do not establish complete type loading, assets, or live Battle state.
"""
import os
import runpy
import struct
import subprocess
import sys
import uuid

from unicorn import UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESI,UC_X86_REG_ESP

if len(sys.argv)!=5:
    raise SystemExit('usage: original_vehicle_readers_check.py game.exe unit_flags_case real_map_case game_dir')
executable,native,fixture,game=sys.argv[1:]
lines=subprocess.check_output([fixture,game,'--base-rule-art']).decode('ascii').splitlines()
raw={'R':[],'A':[]}; current=None; last_kind=None
for line in lines:
    parts=line.split('\t',3)
    assert len(parts)>=3 and parts[0] in raw
    if parts[1]=='S':
        assert len(parts)==3
        current=[parts[2],[]]; raw[parts[0]].append(current); last_kind=parts[0]
    else:
        assert parts[1]=='K' and len(parts)==4 and current is not None and parts[0]==last_kind
        current[1].append((parts[2],parts[3]))
assert [(len(raw[kind]),sum(len(entries) for _,entries in raw[kind])) for kind in ('R','A')]==[(1165,17313),(1278,10339)]
assert all(len({name for name,_ in raw[kind]})==len(raw[kind]) for kind in raw)
sys.argv=[sys.argv[0],executable,native,fixture,game]
registered=runpy.run_path(os.path.join(os.path.dirname(__file__),'original_vehicle_registration_check.py'))
g=registered['g']; m=g['m']; rows=registered['rows']
objects=registered['objects']; object_size=registered['object_size']
registered['loaded']['loaded']['loaded']['trace_enabled'][0]=False
assert not g['active'][0] and not registered['registration_active'][0]
m.cpu.mem_map(0x1700000,0x1000000); m.cpu.mem_map(0x2800000,0x1000000)
cursor=[0x1700000]; heap=[0x2800000]; hashes={}

def reserve(size):
    address=cursor[0]; cursor[0]+=(size+15)&~15
    assert cursor[0]<0x2700000
    return address

def blob(data):
    address=reserve(len(data)); m.cpu.mem_write(address,data); return address

def digest(name):
    if name not in hashes: hashes[name]=g['original_hash'](name)
    return hashes[name]

def signed_digest(value): return value if value<0x80000000 else value-0x100000000

def build_ini(ini,sections):
    table=reserve(len(sections)*8); registry=[]
    for name,entries in sections:
        section=reserve(0x50); registry.append((digest(name),section)); count=len(entries)
        array=reserve(max(1,count)*8); nodes=reserve(max(1,count)*0x40); items=[]
        for index,(key,value) in enumerate(entries):
            node=nodes+index*0x40
            key_ptr=blob(key.encode('ascii')+b'\0'); value_ptr=blob(value.encode('ascii')+b'\0')
            m.word(node+4,nodes+((index+1)%count)*0x40)
            m.word(node+8,nodes+((index-1)%count)*0x40)
            m.word(node+0xc,key_ptr); m.word(node+0x10,value_ptr)
            items.append((digest(key),node))
        assert len({item[0] for item in items})==count, name
        for index,(key_hash,node) in enumerate(sorted(items,key=lambda item:signed_digest(item[0]))):
            m.word(array+8*index,key_hash); m.word(array+8*index+4,node)
        m.word(section+0x18,nodes if count else 0); m.word(section+0x2c,array)
        m.word(section+0x30,count); m.word(section+0x38,1); m.word(section+0x3c,0)
    assert len({item[0] for item in registry})==len(registry)
    for index,(section_hash,section) in enumerate(sorted(registry,key=lambda item:signed_digest(item[0]))):
        m.word(table+8*index,section_hash); m.word(table+8*index+4,section)
    m.word(ini+0x20,table); m.word(ini+0x24,len(registry)); m.word(ini+0x2c,1); m.word(ini+0x30,0)

build_ini(0x100b000,raw['R']); build_ini(0x8397f0,raw['A'])
rules={name:dict(entries) for name,entries in raw['R']}
assert all(name in rules for _,name in rows)
started=[]; returned=[]; writes=[]; section_checks=[0]*len(rows)
reader_chain=[[] for _ in rows]; guid_reads=[]; allocations=[]; service_counts={}

def c_string(address):
    return bytes(m.cpu.mem_read(address,256)).split(b'\0',1)[0].decode('ascii')

def supplied_guid():
    target=m.argument(0); section=c_string(m.argument(1)); key=c_string(m.argument(2))
    assert started and m.cpu.reg_read(UC_X86_REG_ECX)==0x100b000
    assert section==started[-1] and key=='Locomotor'
    value=rules[section].get(key)
    result=uuid.UUID(value).bytes_le if value is not None else struct.pack('<IIII',*(m.argument(i) for i in range(3,7)))
    m.cpu.mem_write(target,result); m.cpu.reg_write(UC_X86_REG_EAX,target)
    guid_reads.append((section,value is not None))

def supplied_allocation():
    size=m.argument(0); assert 0<size<0x100000
    address=heap[0]; heap[0]+=(size+15)&~15
    assert heap[0]<0x3800000
    allocations.append(size); m.cpu.reg_write(UC_X86_REG_EAX,address)

m.bind(0x50da00,28,supplied_guid); m.cpu.ctl_remove_cache(0x50da00,0x50da03)
for address in (0x782017,0x782630,0x78266e):
    m.bind(address,0,supplied_allocation); m.cpu.ctl_remove_cache(address,address+3)
allowed_services={0x50da00,0x78969b,0x782017,0x782630,0x78266e,0x40ffd0,
                  0x7831f0,0x5956c0,0x781d3b,0x50fa40,0x5d6730}

def trace(cpu,address,size,user):
    if address in m.callbacks:
        assert address in allowed_services, hex(address)
        service_counts[address]=service_counts.get(address,0)+1
    if address==0x65234d:
        cpu.emu_stop(); return
    if address==0x652340:
        index=len(started); assert index<len(rows)
        obj=cpu.reg_read(UC_X86_REG_ECX)
        assert obj==objects+index*object_size and bytes(cpu.mem_read(obj+0x205,1))==b'\x01'
        started.append(rows[index][1])
    if address in (0x70ab40,0x6daf60,0x5d7810,0x4107e0):
        assert started and len(returned)+1==len(started)
        assert cpu.reg_read(UC_X86_REG_ECX)==objects+len(returned)*object_size
        reader_chain[len(returned)].append(address)
    if address==0x50c9b0 and started and len(returned)+1==len(started):
        if cpu.reg_read(UC_X86_REG_ECX)==0x100b000 and c_string(m.argument(0))==started[-1]:
            section_checks[len(returned)]+=1
    if address==0x652343:
        index=len(returned); assert index<len(rows)
        assert cpu.reg_read(UC_X86_REG_EAX)&255==1
        obj=objects+index*object_size
        assert c_string(obj+0x24)==rows[index][1] and bytes(cpu.mem_read(obj+0x205,1))==b'\x01'
        assert reader_chain[index]==[0x70ab40,0x6daf60,0x5d7810,0x4107e0]
        returned.append(rows[index][1])

def trace_write(cpu,access,address,size,value,user):
    if objects<=address<objects+len(rows)*object_size:
        index=(address-objects)//object_size; target=objects+index*object_size+0x205
        if address<=target<address+size: writes.append((index,address-target,size,value))

m.cpu.hook_add(UC_HOOK_CODE,trace); m.cpu.hook_add(UC_HOOK_MEM_WRITE,trace_write)
m.cpu.reg_write(UC_X86_REG_ESI,0x100b000); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
m.cpu.emu_start(0x65232a,0x103e000,count=30000000)
assert m.cpu.reg_read(UC_X86_REG_EIP)==0x65234d
assert started==returned==[name for _,name in rows]
assert section_checks==[1]*len(rows) and not writes
assert len(guid_reads)==55 and [name for name,present in guid_reads if not present]==['DeathDummy']
assert service_counts=={0x50da00:55,0x78969b:1771,0x782017:996,0x40ffd0:494,
                        0x7831f0:425,0x5956c0:745,0x781d3b:328,0x50fa40:295,0x5d6730:51}
assert len(allocations)==996
print('base x86 UnitType readers reach all 55 installed vehicles with supplied services')
