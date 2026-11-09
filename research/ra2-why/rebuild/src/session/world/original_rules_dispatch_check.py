#!/usr/bin/env python3
"""Check the original rules reload order around one supplied UnitType.

Base 006437F0 runs with supplied empty section enumerations and supplied rules
services. Original type dispatch and all 30 mission readers run. The one-Unit
registry, initial mission bytes, INI caches, and runtime state remain supplied.
This is not a complete rules load or Battle setup.

With --vehicle, the original VehicleTypes loop reads the installed AMCV entry
through a supplied one-entry enumeration and searches a supplied Unit registry.
"""
import os
import runpy
import struct
import subprocess
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP

if len(sys.argv) not in (5,6) or (len(sys.argv)==6 and sys.argv[5]!='--vehicle'):
    raise SystemExit('usage: original_rules_dispatch_check.py game.exe unit_flags_case real_map_case game_dir [--vehicle]')
real_vehicle=len(sys.argv)==6
if real_vehicle:
    entry=subprocess.check_output([sys.argv[3],sys.argv[4],'--amcv-vehicle-entry'],text=True).strip().split('\t')
    assert entry==['1','AMCV']
sys.argv=[sys.argv[0],*sys.argv[1:5],'--scanner']
loaded=runpy.run_path(os.path.join(os.path.dirname(__file__),'original_mission_dispatch_check.py'))
g=loaded['g']; m=g['m']; rules=0x14b0000; ini=0x100b000
services=(0x647e60,0x64bc10,0x64bba0,0x64ba50,0x64c080,0x64c0f0,0x64c1d0,
          0x64c240,0x64d9e0,0x64b690,0x64c2b0,0x64d490,0x64d610,0x64d850,
          0x647f40,0x64db10,0x6469b0,0x646c60,0x647b60,0x647d40,0x647de0,
          0x643e10,0x643b80)
before=services[:15]; after=services[15:]
events=[]; enumerations=[]; post_flags=[]
vehicle_keys=[]; vehicle_lookups=[]; vehicle_copies=[]
vehicle_ini,vehicle_cache,vehicle_section,vehicle_array=0x14e0000,0x14e0100,0x14e0200,0x14e0300
vehicle_node,vehicle_value,vehicle_key=0x14e0400,0x14e0500,0x14e0600

def supplied_rule_service(address):
    def invoke():
        assert m.cpu.reg_read(UC_X86_REG_ECX)==rules and m.argument(0)==ini
        events.append(address); m.cpu.reg_write(UC_X86_REG_EAX,1)
    return invoke

def empty_section():
    assert m.cpu.reg_read(UC_X86_REG_ECX)==ini
    name=bytes(m.cpu.mem_read(m.argument(0),64)).split(b'\0',1)[0].decode('ascii')
    enumerations.append(name); m.cpu.reg_write(UC_X86_REG_EAX,int(real_vehicle and name=='VehicleTypes'))

def supplied_vehicle_key():
    assert real_vehicle and m.cpu.reg_read(UC_X86_REG_ECX)==ini
    section=bytes(m.cpu.mem_read(m.argument(0),32)).split(b'\0',1)[0]
    assert section==b'VehicleTypes' and m.argument(1)==0
    vehicle_keys.append('1'); m.cpu.reg_write(UC_X86_REG_EAX,vehicle_key)

def supplied_post():
    assert m.cpu.reg_read(UC_X86_REG_ECX)==rules and m.argument(0)==ini
    post_flags.append(m.argument(1)); events.append(0x64dd90)
    m.cpu.reg_write(UC_X86_REG_EAX,1)

def supplied_ini_finalize():
    assert m.cpu.reg_read(UC_X86_REG_ECX)==ini
    events.append(0x6e8ff0); m.cpu.reg_write(UC_X86_REG_EAX,1)

def trace(cpu,address,size,user):
    if real_vehicle and address==0x50e9a0 and not g['active'][0]:
        name=bytes(cpu.mem_read(m.argument(0),32)).split(b'\0',1)[0]
        assert name==b'VehicleTypes' and cpu.reg_read(UC_X86_REG_ECX)==ini
        assert bytes(cpu.mem_read(m.argument(1),2))==b'1\0'
        cpu.reg_write(UC_X86_REG_ECX,vehicle_ini)
    if real_vehicle and address==0x7823d0 and not g['active'][0] and m.argument(1)==vehicle_value:
        vehicle_copies.append(1)
    if real_vehicle and address==0x70b540 and not g['active'][0]:
        assert bytes(cpu.mem_read(cpu.reg_read(UC_X86_REG_ECX),5))==b'AMCV\0'
        vehicle_lookups.append('entry')
    if real_vehicle and address in (0x782017,0x70a600) and vehicle_lookups==['entry']:
        raise AssertionError('existing AMCV lookup attempted allocation or construction')
    if real_vehicle and address==0x643a89:
        assert cpu.reg_read(UC_X86_REG_EAX)==m.TYPE
        vehicle_lookups.append('return')
    if address==0x643af9:
        assert cpu.reg_read(UC_X86_REG_ECX)==rules and m.read(cpu.reg_read(UC_X86_REG_ESP))==ini
        events.append(0x652270)
        if real_vehicle: g['active'][0]=True

for address in services: m.bind(address,4,supplied_rule_service(address))
m.bind(0x50cb00,4,empty_section)
if real_vehicle: m.bind(0x50ce00,8,supplied_vehicle_key)
m.bind(0x6e8ff0,0,supplied_ini_finalize)
m.bind(0x64dd90,8,supplied_post)
m.cpu.hook_add(UC_HOOK_CODE,trace)
if real_vehicle:
    m.word(vehicle_ini+0x24,1); m.word(vehicle_ini+0x30,vehicle_cache)
    m.word(vehicle_cache,g['original_hash']('VehicleTypes')); m.word(vehicle_cache+4,vehicle_section)
    m.word(vehicle_section+0x2c,vehicle_array); m.word(vehicle_section+0x30,1)
    m.word(vehicle_section+0x38,1); m.word(vehicle_section+0x3c,0)
    m.word(vehicle_array,g['original_hash']('1')); m.word(vehicle_array+4,vehicle_node)
    m.word(vehicle_node+0x10,vehicle_value)
    m.cpu.mem_write(vehicle_value,b'AMCV\0'); m.cpu.mem_write(vehicle_key,b'1\0')

for session,expected_flag in ((0,0),(1,1),(5,0)):
    for count in (0xa35d18,0xa40348,0x8661f0,0xa35ce8,0xa3d288,0xa35d58,
                  0xa40360,0x839ba0,0xa35d00,0x839b10,0xa40330,0xa40bd8,
                  0xa35df8,0xa35e10,0xa35de0,0xa40af0): m.word(count,0)
    m.word(0xa35d4c,0x14c2000); m.word(0x14c2000,m.TYPE); m.word(0xa35d58,1)
    m.word(0xa3d298,session)
    m.word(g['bool_ini']+0x24,1); m.word(g['section']+0x3c,0); m.word(g['art_section']+0x3c,0)
    m.word(0x14c1000+0x18,0)
    m.cpu.mem_write(0xa403b0,loaded['image_missions'])
    m.cpu.mem_write(m.TYPE,g['constructor'])
    assert m.cpu.mem_read(m.TYPE+0x205,1)==b'\x01'
    m.cpu.mem_write(m.TYPE+0x24,b'AMCV\0'); m.cpu.mem_write(m.TYPE+0x1c8,b'AMCV\0')
    m.cpu.mem_write(m.TYPE+0x3d,b'\0'); m.cpu.mem_write(m.TYPE+0x64,b'AMCV\0')
    for name in ('chain_calls','writes','section_entries','section_returns','token_sources',
                 'token_returns','token_calls','token_cursor_checks','token_checks','thread_requests',
                 'vhpscan_tokens','copy_results','format_entries','format_results','color_entries',
                 'color_results','color_scan_results','armor_returns','scan_results'):
        g[name].clear()
    for name in ('section_pending','token_pending','copy_pending','format_pending','color_pending','scan_pending'):
        g[name].clear()
    for name in ('entries','sections','completed','writes','scans'):
        loaded[name].clear()
    loaded['scan_pending'].clear(); loaded['current'][0]=None
    loaded['loaded']['unit_dispatch'].clear()
    events.clear(); enumerations.clear(); post_flags.clear()
    vehicle_keys.clear(); vehicle_lookups.clear(); vehicle_copies.clear()
    if real_vehicle: m.word(vehicle_section+0x3c,0)
    g['vhpscan_in_reader'][0]=False; g['mode']=3; g['active'][0]=not real_vehicle
    m.cpu.reg_write(UC_X86_REG_ECX,rules); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,ini))
    m.cpu.emu_start(0x6437f0,0x103e000,count=1000000)
    g['active'][0]=False
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x103e000 and m.cpu.reg_read(UC_X86_REG_ESP)==0x103f008
    assert enumerations==['Countries','OverlayTypes','SuperWeaponTypes','Warheads','SmudgeTypes',
                          'TerrainTypes','BuildingTypes','VehicleTypes']
    assert events==list(before)+[0x652270]+list(after)+[0x6e8ff0,0x64dd90]
    assert post_flags==[expected_flag]
    if real_vehicle:
        assert vehicle_keys==['1'] and vehicle_copies==[1]
        assert vehicle_lookups==['entry','return'] and m.read(0xa35d58)==1
    assert loaded['loaded']['unit_dispatch']==[0x652340]
    assert g['chain_calls']==[0x70ab40,0x6daf60,0x5d7810,0x4107e0]
    assert g['section_entries']==['AMCV'] and g['section_returns']==[g['section']] and not g['section_pending']
    assert len(g['token_checks'])==len(g['token_calls'])==29 and not g['token_pending']
    assert len(g['copy_results'])==96 and not g['copy_pending']
    assert len(g['format_results'])==8 and not g['format_pending']
    assert len(g['scan_results'])==5 and not g['scan_pending']
    assert g['color_results']==[b'\0\0\0',b'\x80\x80\x80'] and g['armor_returns']==[5]
    assert loaded['entries']==loaded['completed']==list(range(30)) and len(loaded['scans'])==23
    assert loaded['sections']==[name for name,_,_ in loaded['missions']]
    assert loaded['current'][0] is None and not loaded['scan_pending'] and not loaded['writes']
    assert m.cpu.mem_read(m.TYPE+0x205,1)==b'\x01'
    assert m.signed(m.TYPE+0x5a4)==1 and m.signed(m.TYPE+0x500)==0 and m.signed(m.TYPE+0x9c)==5
    assert struct.unpack('<d',bytes(m.cpu.mem_read(m.TYPE+0x340,8)))[0]==3.5

print('base x86 rules reload %sone UnitType and 30 missions in three supplied sessions'%('looks up and dispatches ' if real_vehicle else 'dispatches '))
