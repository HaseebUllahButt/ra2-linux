#!/usr/bin/env python3
"""Check the base dispatcher mission loop with installed mission records.

Base 00652270 dispatches one UnitType and visits 30 mission objects. Each
00594E30 body and its INI readers execute. Section records, initial object
bytes, and decimal scanning are supplied. Initial bytes match the executable
image; earlier startup writes are not checked. Other registries and full rules
loading remain omitted.
"""
import os
import runpy
import struct
import subprocess
import sys

from unicorn import UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESI,UC_X86_REG_ESP

if len(sys.argv)!=5:
    raise SystemExit('usage: original_mission_dispatch_check.py game.exe unit_flags_case real_map_case game_dir')
fixture,game=sys.argv[3:5]
lines=subprocess.check_output([fixture,game,'--mission-rules'],text=True).splitlines()
missions=[]
for line in lines:
    fields=line.split('\t')
    if fields[0]=='S':
        assert len(fields)==4 and int(fields[1])==len(missions) and fields[3] in ('0','1')
        missions.append((fields[2],fields[3]=='1',{}))
    else:
        assert fields[0]=='K' and len(fields)==3 and missions and missions[-1][1]
        assert fields[1] not in missions[-1][2]
        missions[-1][2][fields[1]]=fields[2]
assert len(missions)==30
assert sum(present for _,present,_ in missions)==22

loaded=runpy.run_path(os.path.join(os.path.dirname(__file__),'original_unit_dispatch_check.py'))
g=loaded['g']; m=g['m']
m.cpu.mem_write(0x594e30,bytes(g['hash_cpu'].mem_read(0x594e30,3)))
m.callbacks.pop(0x594e30,None); m.cpu.ctl_remove_cache(0x594e30,0x594e33)
image_missions=bytes(g['hash_cpu'].mem_read(0xa403b0,30*0x20))
assert image_missions==bytes(30*0x20)
m.cpu.mem_write(0xa403b0,image_missions)
tail_ini,cache,section,array=0x14c3000,0x14c3100,0x14c3200,0x14c3300
entries=[]; sections=[]; completed=[]; writes=[]; scans=[]
current=[None]

def c_string(ptr):
    return bytes(m.cpu.mem_read(ptr,128)).split(b'\0',1)[0].decode('ascii')

def prepare(index):
    name,present,values=missions[index]
    assert c_string(m.read(0x7cd774+4*index))==name
    m.word(tail_ini+0x24,int(present)); m.word(tail_ini+0x30,cache)
    m.word(cache,g['original_hash'](name)); m.word(cache+4,section)
    rows=sorted(((g['original_hash'](key),value) for key,value in values.items()),
                key=lambda row:row[0] if row[0]<0x80000000 else row[0]-0x100000000)
    assert len(rows)<64 and len({digest for digest,_ in rows})==len(rows)
    cursor=0x14d0000
    for row,(digest,value) in enumerate(rows):
        node=0x14c4000+row*0x40; raw=value.encode('ascii')+b'\0'
        assert cursor+len(raw)<0x14e0000
        m.cpu.mem_write(cursor,raw); m.word(node+0x10,cursor); cursor+=len(raw)
        m.word(array+8*row,digest); m.word(array+8*row+4,node)
    m.word(section+0x2c,array); m.word(section+0x30,len(rows))
    m.word(section+0x38,1); m.word(section+0x3c,0)

def single(value):
    return struct.unpack('<f',struct.pack('<f',float(value)))[0]

def check_object(index):
    name,present,values=missions[index]
    obj=0xa403b0+index*0x20
    assert m.read(obj)==index
    keys=('NoThreat','Zombie','Recruitable','Paralyzed','Retaliate','Scatter')
    expected=[]
    for key in keys:
        value=values.get(key,'no').lower()
        assert value in ('yes','no')
        expected.append(int(value=='yes'))
    assert bytes(m.cpu.mem_read(obj+4,6))==bytes(expected), name
    rate=single(values.get('Rate','0')) if present else 0.0
    aa=single(values.get('AARate','0')) if present else 0.0
    if aa==0: aa=rate
    assert bytes(m.cpu.mem_read(obj+0x10,8))==struct.pack('<d',rate), name
    assert bytes(m.cpu.mem_read(obj+0x18,8))==struct.pack('<d',aa), name

def tail_scan():
    if current[0] is None:
        g['supplied_scan_float']()
        return
    source,fmt,target=m.argument(0),m.argument(1),m.argument(2)
    assert bytes(m.cpu.mem_read(fmt,3))==b'%f\0'
    text=c_string(source)
    assert text in ('1','.016','.1','.030','.040','.032','.08','.5'), text
    m.cpu.mem_write(target,struct.pack('<f',float(text)))
    m.cpu.reg_write(UC_X86_REG_EAX,1)
    scans.append(text)

def trace(cpu,address,size,user):
    if address==0x652343:
        g['active'][0]=False
    if address==0x594e30:
        index=len(entries); assert index<30 and current[0] is None
        assert cpu.reg_read(UC_X86_REG_ECX)==0xa403b0+index*0x20
        assert m.argument(0)==0x100b000 and m.read(0xa403b0+index*0x20)==index
        prepare(index); current[0]=index; entries.append(index)
    if current[0] is not None and address in (0x50c9b0,0x50f410,0x50e3a0):
        name=missions[current[0]][0]
        assert cpu.reg_read(UC_X86_REG_ECX)==0x100b000 and c_string(m.argument(0))==name
        if address==0x50c9b0: sections.append(name)
        cpu.reg_write(UC_X86_REG_ECX,tail_ini)
    if address==0x652503:
        index=current[0]; assert index is not None
        assert (cpu.reg_read(UC_X86_REG_EAX)&255)==int(missions[index][1])
        check_object(index); completed.append(index); current[0]=None

def trace_write(cpu,access,address,size,value,user):
    if address<=m.TYPE+0x205<address+size: writes.append((address,size,value))

m.cpu.hook_add(UC_HOOK_CODE,trace)
m.cpu.hook_add(UC_HOOK_MEM_WRITE,trace_write)
m.bind(0x783730,0,tail_scan)
for count in (0xa35d18,0xa40348,0x8661f0,0xa35ce8,0xa3d288,0xa35d58,
              0xa40360,0x839ba0,0xa35d00,0x839b10,0xa40330,0xa40bd8,
              0xa35df8,0xa35e10,0xa35de0,0xa40af0): m.word(count,0)
m.word(0xa35d4c,0x14c2000); m.word(0x14c2000,m.TYPE); m.word(0xa35d58,1)
m.word(g['bool_ini']+0x24,1); m.word(g['section']+0x3c,0); m.word(g['art_section']+0x3c,0)
m.word(0x14c1000+0x18,0)
m.cpu.mem_write(m.TYPE,g['constructor'])
assert m.cpu.mem_read(m.TYPE+0x205,1)==b'\x01'
m.cpu.mem_write(m.TYPE+0x24,b'AMCV\0'); m.cpu.mem_write(m.TYPE+0x1c8,b'AMCV\0')
m.cpu.mem_write(m.TYPE+0x3d,b'\0'); m.cpu.mem_write(m.TYPE+0x64,b'AMCV\0')
for name in ('chain_calls','writes','section_entries','section_returns','token_sources',
             'token_returns','token_calls','token_cursor_checks','token_checks','thread_requests',
             'vhpscan_tokens','copy_results','format_entries','format_results','color_entries',
             'color_results','color_scan_results','armor_returns'):
    g[name].clear()
for name in ('section_pending','token_pending','copy_pending','format_pending','color_pending'):
    g[name].clear()
g['vhpscan_in_reader'][0]=False; g['mode']=3; g['active'][0]=True
loaded['unit_dispatch'].clear()
m.cpu.reg_write(UC_X86_REG_ECX,0); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100b000))
m.cpu.emu_start(0x652270,0x103e000,count=1000000)
assert m.cpu.reg_read(UC_X86_REG_EIP)==0x103e000 and m.cpu.reg_read(UC_X86_REG_EAX)&255==1
assert loaded['unit_dispatch']==[0x652340]
assert g['chain_calls']==[0x70ab40,0x6daf60,0x5d7810,0x4107e0]
assert g['section_entries']==['AMCV'] and g['section_returns']==[g['section']] and not g['section_pending']
assert len(g['token_checks'])==len(g['token_calls'])==29 and not g['token_pending']
assert len(g['copy_results'])==96 and not g['copy_pending']
assert len(g['format_results'])==8 and not g['format_pending']
assert g['color_results']==[b'\0\0\0',b'\x80\x80\x80'] and not g['color_pending']
assert g['armor_returns']==[5]
assert entries==completed==list(range(30)) and sections==[name for name,_,_ in missions]
expected_scans=sum(int('Rate' in values)+int('AARate' in values) for _,present,values in missions if present)
assert current[0] is None and len(scans)==expected_scans and not writes
assert m.cpu.mem_read(m.TYPE+0x205,1)==b'\x01'
assert m.signed(m.TYPE+0x5a4)==1 and m.signed(m.TYPE+0x500)==0 and m.signed(m.TYPE+0x9c)==5
assert struct.unpack('<d',bytes(m.cpu.mem_read(m.TYPE+0x340,8)))[0]==3.5
print('base x86 dispatcher checks 22 present and 8 absent mission sections')
