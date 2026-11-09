#!/usr/bin/env python3
"""Check original registration of installed base VehicleTypes.

Base 006437F0 enumerates all 55 supplied VehicleTypes records. Original
0070B540 and UnitType constructors register them. Section storage, allocations,
other registries, and surrounding services are supplied. Execution stops before
type readers; this is not complete rules loading or Battle setup.
"""
import os
import runpy
import struct
import subprocess
import sys

from unicorn import UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP

if len(sys.argv)!=5:
    raise SystemExit('usage: original_vehicle_registration_check.py game.exe unit_flags_case real_map_case game_dir')
fixture,game=sys.argv[3:5]
rows=[tuple(line.split('\t',1)) for line in subprocess.check_output(
    [fixture,game,'--vehicle-all-entries'],text=True).splitlines()]
assert len(rows)==55 and all(len(row)==2 for row in rows) and rows[0]==('1','AMCV')
assert len({name.casefold() for _,name in rows})==len(rows)
sys.argv=[sys.argv[0],*sys.argv[1:5],'--vehicle-create-enum']
loaded=runpy.run_path(os.path.join(os.path.dirname(__file__),'original_rules_dispatch_check.py'))
r=loaded['trace'].__globals__; g=loaded['g']; m=g['m']
# Disable only the earlier filtered fixture's hooks. Original code remains.
r['real_vehicle']=False; r['real_enumeration']=False; r['create_vehicle']=False
g['active'][0]=False
m.cpu.mem_map(0x1500000,0x40000); m.cpu.mem_map(0x1600000,0x40000)
ini,cache,section,array=0x1600000,0x1600100,0x1600200,0x1601000
unit_vector,object_vector,nodes,strings=0x1603000,0x1604000,0x1610000,0x1620000
objects=0x1500000; object_size=0xc10; original_ini=0x100b000; rules=0x14b0000
names=[]; allocated=[]; constructed=[]; returned=[]; count_reads=[]; key_reads=[]; writes=[]
count_pending={}; key_pending={}; value_pending={}
count_results=[]; key_results=[]; value_results=[]
registration_active=[True]

def c_string(ptr):
    return bytes(m.cpu.mem_read(ptr,256)).split(b'\0',1)[0].decode('ascii')

def signed_digest(value): return value if value<0x80000000 else value-0x100000000

m.word(ini+0x20,cache); m.word(ini+0x24,1); m.word(ini+0x2c,1); m.word(ini+0x30,cache)
m.word(cache,g['original_hash']('VehicleTypes')); m.word(cache+4,section)
m.word(section+0x18,nodes); m.word(section+0x2c,array)
m.word(section+0x30,len(rows)); m.word(section+0x38,1); m.word(section+0x3c,0)
indexed=[]; cursor=strings
for index,(key,value) in enumerate(rows):
    key_raw=key.encode('ascii')+b'\0'; value_raw=value.encode('ascii')+b'\0'
    assert cursor+len(key_raw)+len(value_raw)<0x1640000
    key_ptr=cursor; m.cpu.mem_write(cursor,key_raw); cursor+=len(key_raw)
    value_ptr=cursor; m.cpu.mem_write(cursor,value_raw); cursor+=len(value_raw)
    node=nodes+index*0x40
    m.word(node+4,nodes+((index+1)%len(rows))*0x40)
    m.word(node+8,nodes+((index-1)%len(rows))*0x40)
    m.word(node+0xc,key_ptr); m.word(node+0x10,value_ptr)
    indexed.append((g['original_hash'](key),node))
assert len({digest for digest,_ in indexed})==len(rows)
for index,(digest,node) in enumerate(sorted(indexed,key=lambda item:signed_digest(item[0]))):
    m.word(array+8*index,digest); m.word(array+8*index+4,node)

def allocate():
    assert m.argument(0)==object_size and len(allocated)<len(rows)
    address=objects+len(allocated)*object_size
    m.cpu.mem_write(address,bytes([0xa5])*object_size)
    allocated.append(address); m.cpu.reg_write(UC_X86_REG_EAX,address)

def trace(cpu,address,size,user):
    if not registration_active[0]: return
    if address in count_pending:
        name,expected=count_pending.pop(address)
        assert cpu.reg_read(UC_X86_REG_EAX)==expected
        count_results.append((name,expected))
    if address in key_pending:
        key=key_pending.pop(address)
        assert c_string(cpu.reg_read(UC_X86_REG_EAX))==key
        key_results.append(key)
    if address in value_pending:
        target,value=value_pending.pop(address)
        assert cpu.reg_read(UC_X86_REG_EAX)==len(value) and c_string(target)==value
        value_results.append(value)
    if address==0x643a8e:
        cpu.emu_stop(); return
    if address==0x50cb00:
        assert cpu.reg_read(UC_X86_REG_ECX)==original_ini
        name=c_string(m.argument(0)); count_reads.append(name)
        ret=m.read(cpu.reg_read(UC_X86_REG_ESP)); assert ret not in count_pending
        count_pending[ret]=(name,55 if name=='VehicleTypes' else 0)
        cpu.reg_write(UC_X86_REG_ECX,ini)
    if address==0x50ce00:
        assert cpu.reg_read(UC_X86_REG_ECX)==original_ini and c_string(m.argument(0))=='VehicleTypes'
        assert m.argument(1)==len(returned)
        key_reads.append(m.argument(1))
        ret=m.read(cpu.reg_read(UC_X86_REG_ESP)); assert ret not in key_pending
        key_pending[ret]=rows[len(returned)][0]
        cpu.reg_write(UC_X86_REG_ECX,ini)
    if address==0x50e9a0:
        assert cpu.reg_read(UC_X86_REG_ECX)==original_ini and c_string(m.argument(0))=='VehicleTypes'
        assert c_string(m.argument(1))==rows[len(returned)][0]
        ret=m.read(cpu.reg_read(UC_X86_REG_ESP)); assert ret not in value_pending
        value_pending[ret]=(m.argument(3),rows[len(returned)][1])
        cpu.reg_write(UC_X86_REG_ECX,ini)
    if address==0x70b540:
        name=c_string(cpu.reg_read(UC_X86_REG_ECX))
        assert name==rows[len(returned)][1]
        names.append(name)
    if address==0x70a600:
        assert allocated and cpu.reg_read(UC_X86_REG_ECX)==allocated[-1]
        assert c_string(m.argument(0))==rows[len(returned)][1]
        constructed.append(allocated[-1])
    if address==0x643a89:
        assert cpu.reg_read(UC_X86_REG_EAX)==allocated[len(returned)]
        returned.append(cpu.reg_read(UC_X86_REG_EAX))

def trace_write(cpu,access,address,size,value,user):
    if not registration_active[0]: return
    if objects<=address<objects+len(rows)*object_size:
        offset=address-objects
        index=offset//object_size
        target=objects+index*object_size+0x205
        if address<=target<address+size: writes.append((index,address-target,size,value))

m.bind(0x782017,0,allocate); m.cpu.ctl_remove_cache(0x782017,0x78201a)
m.cpu.hook_add(UC_HOOK_CODE,trace); m.cpu.hook_add(UC_HOOK_MEM_WRITE,trace_write)
for count in (0xa35d18,0xa40348,0x8661f0,0xa35ce8,0xa3d288,0xa35d58,
              0xa40360,0x839ba0,0xa35d00,0x839b10,0xa40330,0xa40bd8,
              0xa35df8,0xa35e10,0xa35de0,0xa40af0,0xa40938): m.word(count,0)
for address in (0xa72f98,0xa72fa0,0xa72fa4,0xac0cb8,0xac0cc0,0xac0cc4,
                0xa40ac0,0xa40ac8,0xa40acc): m.word(address,0)
m.word(0xa35d4c,unit_vector); m.word(0xa35d50,len(rows))
m.word(0xa4092c,object_vector); m.word(0xa40930,len(rows))
m.cpu.reg_write(UC_X86_REG_ECX,rules); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,original_ini))
m.cpu.emu_start(0x6437f0,0x103e000,count=10000000)
assert m.cpu.reg_read(UC_X86_REG_EIP)==0x643a8e
assert count_reads==['Countries','OverlayTypes','SuperWeaponTypes','Warheads','SmudgeTypes',
                     'TerrainTypes','BuildingTypes','VehicleTypes']
assert key_reads==list(range(len(rows))) and names==[name for _,name in rows]
assert count_results==[(name,55 if name=='VehicleTypes' else 0) for name in count_reads] and not count_pending
assert key_results==[key for key,_ in rows] and value_results==[name for _,name in rows]
assert not key_pending and not value_pending
assert allocated==constructed==returned and len(returned)==55
assert m.read(0xa35d58)==m.read(0xa40938)==55
assert writes==[(index,0,1,1) for index in range(55)]
for index,(address,(_,name)) in enumerate(zip(returned,rows)):
    assert m.read(unit_vector+index*4)==address and m.read(object_vector+index*4)==address
    assert c_string(address+0x24)==name and m.signed(address+0xb90)==index
    assert bytes(m.cpu.mem_read(address+0x205,1))==b'\x01'
registration_active[0]=False
print('base x86 rules loader registers all 55 installed VehicleTypes before type readers')
