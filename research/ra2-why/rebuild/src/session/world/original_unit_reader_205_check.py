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
In connected-real-speed mode, the original SpeedType and MovementZone readers run.
The connected-speed-wheel mode supplies one synthetic SpeedType override.
The connected-zone-subterranean mode supplies one synthetic MovementZone override.
The connected-real-placement mode runs the original MovementRestrictedTo reader.
The connected-restricted-water mode supplies a synthetic Water override.
The connected-real-armor mode also runs the original Armor reader.
The VHPScan modes run its original reader with a supplied single-token service.
The connected-real-compare modes also run the original string comparison.
The connected-real-token modes run the original tokenizer with supplied
thread storage. Token calls outside VHPScan remain supplied.
The connected-real-section mode also runs the original AMCV section lookup.
The connected-real-copy mode also runs the original string-copy routine.
The connected-real-color mode runs the color reader with supplied decimal
formatting and scanning services for its tested default values.
The connected-real-tokens mode runs the original tokenizer at other calls too.
The connected-real-format mode runs the original formatter; scanning remains supplied.
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

if len(sys.argv) not in (5,6) or (len(sys.argv)==6 and sys.argv[5] not in ('--connected','--connected-real-bool','--connected-real-art','--connected-real-integer','--connected-real-float','--connected-real-speed','--connected-speed-wheel','--connected-zone-subterranean','--connected-real-placement','--connected-restricted-water','--connected-real-armor','--connected-real-vhpscan','--connected-vhpscan-strong','--connected-real-compare','--connected-compare-strong','--connected-real-token','--connected-token-strong','--connected-real-section','--connected-real-copy','--connected-real-color','--connected-real-tokens','--connected-real-format')):
    raise SystemExit('usage: original_unit_reader_205_check.py game.exe unit_flags_case real_map_case game_dir [mode]')
executable,native,fixture,game=sys.argv[1:5]
connected=len(sys.argv)==6
real_format=connected and sys.argv[5]=='--connected-real-format'
real_tokens=connected and (real_format or sys.argv[5]=='--connected-real-tokens')
real_color=connected and (real_tokens or sys.argv[5]=='--connected-real-color')
real_copy=connected and (real_color or sys.argv[5]=='--connected-real-copy')
real_section=connected and (real_copy or sys.argv[5]=='--connected-real-section')
real_token=connected and (real_section or sys.argv[5] in ('--connected-real-token','--connected-token-strong'))
real_compare=connected and (real_token or sys.argv[5] in ('--connected-real-compare','--connected-compare-strong'))
real_vhpscan=connected and (real_compare or sys.argv[5] in ('--connected-real-vhpscan','--connected-vhpscan-strong'))
vhpscan_override=connected and sys.argv[5] in ('--connected-vhpscan-strong','--connected-compare-strong','--connected-token-strong')
real_armor=connected and (real_vhpscan or sys.argv[5]=='--connected-real-armor')
real_restricted=connected and (real_armor or sys.argv[5] in ('--connected-real-placement','--connected-restricted-water'))
real_bool=connected and (real_restricted or sys.argv[5] in ('--connected-real-bool','--connected-real-art','--connected-real-integer','--connected-real-float','--connected-real-speed','--connected-speed-wheel','--connected-zone-subterranean'))
real_art=connected and (real_restricted or sys.argv[5] in ('--connected-real-art','--connected-real-integer','--connected-real-float','--connected-real-speed','--connected-speed-wheel','--connected-zone-subterranean'))
real_integer=connected and (real_restricted or sys.argv[5] in ('--connected-real-integer','--connected-real-float','--connected-real-speed','--connected-speed-wheel','--connected-zone-subterranean'))
real_float=connected and (real_restricted or sys.argv[5] in ('--connected-real-float','--connected-real-speed','--connected-speed-wheel','--connected-zone-subterranean'))
real_speed=connected and (real_restricted or sys.argv[5] in ('--connected-real-speed','--connected-speed-wheel','--connected-zone-subterranean'))
speed_override=connected and sys.argv[5]=='--connected-speed-wheel'
zone_override=connected and sys.argv[5]=='--connected-zone-subterranean'
restricted_override=connected and sys.argv[5]=='--connected-restricted-water'
sys.argv=[sys.argv[0],executable,native,fixture,game]
if connected:
    techno=runpy.run_path(os.path.join(os.path.dirname(__file__),'original_techno_reader_205_check.py'))
    base=techno['base']
    lines=subprocess.check_output([fixture,game,'--amcv-all-rules'],text=True).splitlines()
    base['installed'].update({key.casefold():value for key,value in (line.split('\t',1) for line in lines)})
    raw_rules=dict(line.split('\t',1) for line in lines)
    if real_vhpscan:
        assert not any(key.casefold()=='vhpscan' for key in raw_rules)
        if vhpscan_override: raw_rules['VHPScan']='Strong'
    if real_restricted:
        assert not any(key.casefold()=='movementrestrictedto' for key in raw_rules)
        if restricted_override: raw_rules['MovementRestrictedTo']='Water'
    if zone_override:
        assert raw_rules.get('MovementZone')=='Normal'
        raw_rules['MovementZone']='Subterannean'
    if speed_override:
        assert not any(key.casefold()=='speedtype' for key in raw_rules)
        raw_rules['SpeedType']='Wheel'
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
speed_entries=[]
speed_value_range=[]
speed_copies=[]
zone_entries=[]
zone_value_range=[]
zone_copies=[]
restricted_entries=[]
restricted_value_range=[]
restricted_copies=[]
armor_entries=[]
armor_value_range=[]
armor_copies=[]
armor_returns=[]
vhpscan_entries=[]
vhpscan_returns=[]
vhpscan_tokens=[]
vhpscan_value_range=[]
vhpscan_copies=[]
vhpscan_in_reader=[False]
compare_entries=[]
compare_pending={}
compare_results=[]
thread_requests=[]
token_sources=[]
token_returns=[]
token_calls=[]
token_cursor_checks=[]
token_pending={}
token_checks=[]
section_entries=[]
section_pending={}
section_returns=[]
copy_pending={}
copy_results=[]
color_pending={}
color_entries=[]
color_results=[]
color_scan_results=[]
format_entries=[]
format_pending={}
format_results=[]

def trace_write(cpu,access,address,size,value,user):
    if active[0] and address<=m.TYPE+0x205<address+size:
        writes.append((address,size,value))

def trace_art_value(cpu,access,address,size,value,user):
    if active[0] and real_art and mode==3:
        for key,(start,end) in art_value_ranges.items():
            if address<end and address+size>start: art_value_reads.add(key)

def note_copy_source(source):
    if active[0] and real_art and mode==3:
        for key,(start,end) in art_value_ranges.items():
            if start<=source<end: art_value_reads.add(key)
        if speed_override and speed_value_range and speed_value_range[0]<=source<speed_value_range[1]:
            speed_copies.append(source)
        if real_speed and zone_value_range and zone_value_range[0]<=source<zone_value_range[1]:
            zone_copies.append(source)
        if restricted_override and restricted_value_range and restricted_value_range[0]<=source<restricted_value_range[1]:
            restricted_copies.append(source)
        if real_armor and armor_value_range and armor_value_range[0]<=source<armor_value_range[1]:
            armor_copies.append(source)
        if vhpscan_override and vhpscan_value_range and vhpscan_value_range[0]<=source<vhpscan_value_range[1]:
            vhpscan_copies.append(source)

def checked_copy_string():
    note_copy_source(m.argument(1))
    base['copy_string']()

def supplied_single_token():
    if not vhpscan_in_reader[0]:
        m.cpu.reg_write(UC_X86_REG_EAX,0)
        return
    source,delimiters=m.argument(0),m.argument(1)
    text=bytes(m.cpu.mem_read(source,128)).split(b'\0',1)[0]
    separator=bytes(m.cpu.mem_read(delimiters,16)).split(b'\0',1)[0]
    assert text in (b'None',b'Strong') and not any(c in separator for c in text)
    vhpscan_tokens.append(text)
    m.cpu.reg_write(UC_X86_REG_EAX,source)

def supplied_thread_storage():
    thread_requests.append(1)
    m.cpu.reg_write(UC_X86_REG_EAX,0x14c1000)

def supplied_scan_float():
    source,format_ptr,target=m.argument(0),m.argument(1),m.argument(2)
    if real_color and color_pending:
        assert bytes(m.cpu.mem_read(format_ptr,9))==b'%d,%d,%d\0'
        text=bytes(m.cpu.mem_read(source,64)).split(b'\0',1)[0].decode('ascii')
        parts=text.split(',')
        assert len(parts)==3 and all(part.isdecimal() and int(part)<=255 for part in parts)
        for i,part in enumerate(parts): m.word(m.argument(i+2),int(part))
        color_scan_results.append(tuple(map(int,parts)))
        m.cpu.reg_write(UC_X86_REG_EAX,3)
        return
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
        if real_format and mode==3 and address in format_pending:
            target,expected=format_pending.pop(address)
            assert cpu.reg_read(UC_X86_REG_EAX)==len(expected)
            assert bytes(cpu.mem_read(target,len(expected)+1))==expected+b'\0'
            format_results.append(expected)
        if real_tokens and mode==3 and address in token_pending:
            expected_return,expected_cursor,terminator=token_pending.pop(address)
            assert cpu.reg_read(UC_X86_REG_EAX)==expected_return
            assert m.read(0x14c1000+0x18)==expected_cursor
            if terminator: assert bytes(cpu.mem_read(terminator,1))==b'\0'
            token_checks.append(1)
        if real_color and mode==3 and address in color_pending:
            target,expected=color_pending.pop(address)
            assert cpu.reg_read(UC_X86_REG_EAX)==target and bytes(cpu.mem_read(target,3))==expected
            color_results.append(expected)
        if real_copy and mode==3 and address in copy_pending:
            target,expected=copy_pending.pop(address)
            assert cpu.reg_read(UC_X86_REG_EAX)==target
            assert bytes(cpu.mem_read(target,len(expected)))==expected
            copy_results.append(1)
        if real_section and mode==3 and address in section_pending:
            section_pending.pop(address)
            section_returns.append(cpu.reg_read(UC_X86_REG_EAX))
        if real_compare and mode==3 and address in compare_pending:
            left,right=compare_pending.pop(address)
            equal=cpu.reg_read(UC_X86_REG_EAX)==0
            assert equal==(left==right), (left,right)
            compare_results.append((left,right,equal))
        if real_integer and mode==3 and address in integer_pending:
            key,expected=integer_pending.pop(address)
            assert cpu.reg_read(UC_X86_REG_EAX)==expected, key
            integer_checked.add(key)
        if address in (0x70ab40,0x6daf60,0x5d7810,0x4107e0): chain_calls.append(address)
        if address==0x6daf60: calls['parent']+=1
        if real_format and mode==3 and address==0x7820f4:
            fmt=bytes(cpu.mem_read(m.argument(1),128)).split(b'\0',1)[0].decode('ascii')
            assert fmt in ('%d,%d,%d','FiringSyncFrame%d','BurstDelay%d')
            count=3 if fmt=='%d,%d,%d' else 1
            values=[m.argument(i+2) for i in range(count)]
            values=[value if value<0x80000000 else value-0x100000000 for value in values]
            expected=(fmt % tuple(values)).encode('ascii')
            ret=struct.unpack('<I',bytes(cpu.mem_read(cpu.reg_read(UC_X86_REG_ESP),4)))[0]
            assert ret not in format_pending
            format_pending[ret]=(m.argument(0),expected)
            format_entries.append(fmt)
            calls['format']+=1
        if real_color and mode==3 and address==0x46a010:
            target,section_ptr,key_ptr,source=(m.argument(i) for i in range(4))
            section_name=bytes(cpu.mem_read(section_ptr,64)).split(b'\0',1)[0].decode('ascii')
            key=bytes(cpu.mem_read(key_ptr,64)).split(b'\0',1)[0].decode('ascii')
            incoming=cpu.reg_read(UC_X86_REG_ECX)
            assert (section_name,incoming) in (('AMCV',0x100b000),('MCV',0x8397f0)), (section_name,key,hex(incoming))
            records=raw_rules if section_name=='AMCV' else raw_art
            assert not any(name.casefold()==key.casefold() for name in records)
            ret=struct.unpack('<I',bytes(cpu.mem_read(cpu.reg_read(UC_X86_REG_ESP),4)))[0]
            assert ret not in color_pending
            color_pending[ret]=(target,bytes(cpu.mem_read(source,3)))
            color_entries.append(key)
        if real_copy and mode==3 and address==0x7823d0:
            target,source,count=m.argument(0),m.argument(1),m.argument(2)
            assert count<=1024
            raw=bytes(cpu.mem_read(source,count)).split(b'\0',1)[0][:count]
            expected=raw+bytes(count-len(raw))
            ret=struct.unpack('<I',bytes(cpu.mem_read(cpu.reg_read(UC_X86_REG_ESP),4)))[0]
            assert ret not in copy_pending
            copy_pending[ret]=(target,expected)
            note_copy_source(source)
        if real_section and mode==3 and address==0x50c9b0:
            section_name=bytes(cpu.mem_read(m.argument(0),64)).split(b'\0',1)[0].decode('ascii')
            assert cpu.reg_read(UC_X86_REG_ECX)==0x100b000 and section_name=='AMCV'
            ret=struct.unpack('<I',bytes(cpu.mem_read(cpu.reg_read(UC_X86_REG_ESP),4)))[0]
            assert ret not in section_pending
            section_pending[ret]=True
            section_entries.append(section_name)
            cpu.reg_write(UC_X86_REG_ECX,bool_ini)
        if real_compare and mode==3 and address==0x781f20:
            left=bytes(cpu.mem_read(m.argument(0),64)).split(b'\0',1)[0].decode('ascii')
            right=bytes(cpu.mem_read(m.argument(1),64)).split(b'\0',1)[0].decode('ascii')
            compare_entries.append((left.lower(),right.lower()))
            ret=struct.unpack('<I',bytes(cpu.mem_read(cpu.reg_read(UC_X86_REG_ESP),4)))[0]
            assert ret not in compare_pending
            compare_pending[ret]=(left.lower(),right.lower())
            calls['compare']+=1
        if real_speed and mode==3 and address==0x46c400:
            section=bytes(cpu.mem_read(m.argument(0),64)).split(b'\0',1)[0].decode('ascii')
            key=bytes(cpu.mem_read(m.argument(1),64)).split(b'\0',1)[0].decode('ascii')
            assert cpu.reg_read(UC_X86_REG_ECX)==0x100b000 and section=='AMCV' and key=='SpeedType'
            speed_entries.append((section,key,m.argument(2)))
        if real_speed and mode==3 and address==0x46a300:
            section=bytes(cpu.mem_read(m.argument(0),64)).split(b'\0',1)[0].decode('ascii')
            key=bytes(cpu.mem_read(m.argument(1),64)).split(b'\0',1)[0].decode('ascii')
            assert cpu.reg_read(UC_X86_REG_ECX)==0x100b000 and section=='AMCV' and key=='MovementZone'
            zone_entries.append((section,key,m.argument(2)))
        if real_restricted and mode==3 and address==0x46a970:
            section=bytes(cpu.mem_read(m.argument(0),64)).split(b'\0',1)[0].decode('ascii')
            key=bytes(cpu.mem_read(m.argument(1),64)).split(b'\0',1)[0].decode('ascii')
            assert cpu.reg_read(UC_X86_REG_ECX)==0x100b000 and section=='AMCV' and key=='MovementRestrictedTo'
            restricted_entries.append((section,key,m.argument(2)))
        if real_armor and mode==3 and address==0x46a8b0:
            section=bytes(cpu.mem_read(m.argument(0),64)).split(b'\0',1)[0].decode('ascii')
            key=bytes(cpu.mem_read(m.argument(1),64)).split(b'\0',1)[0].decode('ascii')
            assert cpu.reg_read(UC_X86_REG_ECX)==0x100b000 and section=='AMCV' and key=='Armor'
            armor_entries.append((section,key,m.argument(2)))
        if real_armor and mode==3 and address==0x5d7a01:
            armor_returns.append(cpu.reg_read(UC_X86_REG_EAX))
        if real_vhpscan and mode==3 and address==0x46c9d0:
            section=bytes(cpu.mem_read(m.argument(0),64)).split(b'\0',1)[0].decode('ascii')
            key=bytes(cpu.mem_read(m.argument(1),64)).split(b'\0',1)[0].decode('ascii')
            assert cpu.reg_read(UC_X86_REG_ECX)==0x100b000 and section=='AMCV' and key=='VHPScan'
            vhpscan_entries.append((section,key,m.argument(2)))
            vhpscan_in_reader[0]=True
        if real_vhpscan and mode==3 and address==0x6db34b:
            vhpscan_returns.append(cpu.reg_read(UC_X86_REG_EAX))
            if real_token:
                assert len(token_sources)==1
                source,length=token_sources[0]
                token_cursor_checks.append(m.read(0x14c1000+0x18)==source+length)
            vhpscan_in_reader[0]=False
        if real_token and mode==3 and address==0x782ec2:
            token_calls.append((m.argument(0),m.argument(1)))
            if real_tokens:
                source=m.argument(0) or m.read(0x14c1000+0x18)
                assert source
                def read_c_string(ptr):
                    data=bytearray()
                    for offset in range(4096):
                        char=bytes(cpu.mem_read(ptr+offset,1))[0]
                        if char==0: return bytes(data)
                        data.append(char)
                    raise AssertionError('unterminated token input')
                text=read_c_string(source)
                separators=read_c_string(m.argument(1))
                first=0
                while first<len(text) and text[first] in separators: first+=1
                end=first
                while end<len(text) and text[end] not in separators: end+=1
                expected_return=source+first if end>first else 0
                terminator=source+end if end<len(text) else 0
                expected_cursor=source+end+(1 if terminator else 0)
                ret=struct.unpack('<I',bytes(cpu.mem_read(cpu.reg_read(UC_X86_REG_ESP),4)))[0]
                assert ret not in token_pending
                token_pending[ret]=(expected_return,expected_cursor,terminator)
            if not vhpscan_in_reader[0] and not real_tokens:
                cpu.reg_write(UC_X86_REG_EAX,0)
                cpu.reg_write(UC_X86_REG_EIP,0x14c0000)
            elif vhpscan_in_reader[0]:
                source=m.argument(0)
                token=bytes(cpu.mem_read(source,128)).split(b'\0',1)[0]
                assert token in (b'None',b'Strong')
                vhpscan_tokens.append(token)
                token_sources.append((source,len(token)))
        if real_token and mode==3 and address==0x782f63:
            if vhpscan_in_reader[0]: token_returns.append(cpu.reg_read(UC_X86_REG_EAX))
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
    if real_color and color_pending:
        target,fmt=(m.argument(i) for i in range(2))
        assert bytes(m.cpu.mem_read(fmt,9))==b'%d,%d,%d\0'
        numbers=[m.argument(i) for i in range(2,5)]
        assert all(value<=255 for value in numbers)
        raw=','.join(map(str,numbers)).encode('ascii')
        m.cpu.mem_write(target,raw+b'\0')
        m.cpu.reg_write(UC_X86_REG_EAX,len(raw))
        return
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
if real_vhpscan: m.bind(0x782ec2,0,supplied_single_token)
if real_token: m.bind(0x78969b,0,supplied_thread_storage)
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
    if real_token: m.cpu.mem_write(0x14c0000,b'\xc3')
    section_cache,section,bool_ini,array=0x1301000,0x1302000,0x1303000,0x1304000
    strings=0x1380000
    for index,(digest,value) in enumerate(entries):
        node=0x1320000+index*0x40; encoded=value.encode('utf-8')+b'\0'
        assert strings+len(encoded)<0x14c0000
        if speed_override and digest==original_hash('SpeedType'):
            assert value=='Wheel'
            speed_value_range[:]=[strings,strings+len(encoded)]
        if real_speed and digest==original_hash('MovementZone'):
            assert value==('Subterannean' if zone_override else 'Normal')
            zone_value_range[:]=[strings,strings+len(encoded)]
        if restricted_override and digest==original_hash('MovementRestrictedTo'):
            assert value=='Water'
            restricted_value_range[:]=[strings,strings+len(encoded)]
        if real_armor and digest==original_hash('Armor'):
            assert value=='heavy'
            armor_value_range[:]=[strings,strings+len(encoded)]
        if vhpscan_override and digest==original_hash('VHPScan'):
            assert value=='Strong'
            vhpscan_value_range[:]=[strings,strings+len(encoded)]
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
            assert strings+len(encoded)<0x14c0000
            art_value_ranges[key]=(strings,strings+len(encoded))
            m.cpu.mem_write(strings,encoded); m.word(node+0x10,strings); strings+=len(encoded)
            m.word(art_array+8*index,digest); m.word(art_array+8*index+4,node)
        m.word(art_cache,original_hash('MCV')); m.word(art_cache+4,art_section)
        m.word(art_section+0x2c,art_array); m.word(art_section+0x30,len(art_values)); m.word(art_section+0x38,1)
        m.word(art_ini+0x24,1); m.word(art_ini+0x30,art_cache)
        string_prologue=bytes(hash_cpu.mem_read(0x50e9a0,3))
        integer_prologue=bytes(hash_cpu.mem_read(0x50d7f0,3))
        float_prologue=bytes(hash_cpu.mem_read(0x50e3a0,7))
        speed_prologue=bytes(hash_cpu.mem_read(0x46c400,3))
        restricted_prologue=bytes(hash_cpu.mem_read(0x46a970,3))
        armor_prologue=bytes(hash_cpu.mem_read(0x46a8b0,3))
        vhpscan_prologue=bytes(hash_cpu.mem_read(0x46c9d0,3))
        compare_prologue=bytes(hash_cpu.mem_read(0x781f20,3))
        token_prologue=bytes(hash_cpu.mem_read(0x782ec2,3))
        section_prologue=bytes(hash_cpu.mem_read(0x50c9b0,3))
        copy_prologue=bytes(hash_cpu.mem_read(0x7823d0,3))
        color_prologue=bytes(hash_cpu.mem_read(0x46a010,3))
        format_prologue=bytes(hash_cpu.mem_read(0x7820f4,3))
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
            if real_speed:
                m.cpu.mem_write(0x46c400,speed_prologue); m.callbacks.pop(0x46c400,None)
                m.cpu.ctl_remove_cache(0x46c400,0x46c403)
            if real_restricted:
                m.cpu.mem_write(0x46a970,restricted_prologue); m.callbacks.pop(0x46a970,None)
                m.cpu.ctl_remove_cache(0x46a970,0x46a973)
            if real_armor:
                m.cpu.mem_write(0x46a8b0,armor_prologue); m.callbacks.pop(0x46a8b0,None)
                m.cpu.ctl_remove_cache(0x46a8b0,0x46a8b3)
            if real_vhpscan:
                m.cpu.mem_write(0x46c9d0,vhpscan_prologue); m.callbacks.pop(0x46c9d0,None)
                m.cpu.ctl_remove_cache(0x46c9d0,0x46c9d3)
            if real_compare:
                m.cpu.mem_write(0x781f20,compare_prologue); m.callbacks.pop(0x781f20,None)
                m.cpu.ctl_remove_cache(0x781f20,0x781f23)
            if real_token:
                m.word(0x14c1000+0x18,0)
                m.cpu.mem_write(0x782ec2,token_prologue); m.callbacks.pop(0x782ec2,None)
                m.cpu.ctl_remove_cache(0x782ec2,0x782ec5)
            if real_section:
                m.cpu.mem_write(0x50c9b0,section_prologue); m.callbacks.pop(0x50c9b0,None)
                m.cpu.ctl_remove_cache(0x50c9b0,0x50c9b3)
            if real_copy:
                m.cpu.mem_write(0x7823d0,copy_prologue); m.callbacks.pop(0x7823d0,None)
                m.cpu.ctl_remove_cache(0x7823d0,0x7823d3)
            if real_color:
                m.cpu.mem_write(0x46a010,color_prologue); m.callbacks.pop(0x46a010,None)
                m.cpu.ctl_remove_cache(0x46a010,0x46a013)
            if real_format:
                m.cpu.mem_write(0x7820f4,format_prologue); m.callbacks.pop(0x7820f4,None)
                m.cpu.ctl_remove_cache(0x7820f4,0x7820f7)
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
            if real_speed:
                m.bind(0x46c400,12,base['integer']); m.cpu.ctl_remove_cache(0x46c400,0x46c403)
            if real_restricted:
                m.bind(0x46a970,12,base['integer']); m.cpu.ctl_remove_cache(0x46a970,0x46a973)
            if real_armor:
                m.bind(0x46a8b0,12,base['integer']); m.cpu.ctl_remove_cache(0x46a8b0,0x46a8b3)
            if real_vhpscan:
                m.bind(0x46c9d0,12,base['integer']); m.cpu.ctl_remove_cache(0x46c9d0,0x46c9d3)
            if real_compare:
                m.bind(0x781f20,0,compare_names); m.cpu.ctl_remove_cache(0x781f20,0x781f23)
            if real_token:
                m.bind(0x782ec2,0,supplied_single_token); m.cpu.ctl_remove_cache(0x782ec2,0x782ec5)
            if real_section:
                m.bind(0x50c9b0,4,base['section_lookup']); m.cpu.ctl_remove_cache(0x50c9b0,0x50c9b3)
            if real_copy:
                m.bind(0x7823d0,0,checked_copy_string); m.cpu.ctl_remove_cache(0x7823d0,0x7823d3)
            if real_color:
                m.bind(0x46a010,16,base['color_reader']); m.cpu.ctl_remove_cache(0x46a010,0x46a013)
            if real_format:
                m.bind(0x7820f4,0,format_key); m.cpu.ctl_remove_cache(0x7820f4,0x7820f7)
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
    speed_entries.clear()
    speed_copies.clear()
    zone_entries.clear()
    zone_copies.clear()
    restricted_entries.clear()
    restricted_copies.clear()
    armor_entries.clear()
    armor_copies.clear()
    armor_returns.clear()
    vhpscan_entries.clear()
    vhpscan_returns.clear()
    vhpscan_tokens.clear()
    vhpscan_copies.clear()
    vhpscan_in_reader[0]=False
    compare_entries.clear()
    compare_pending.clear()
    compare_results.clear()
    thread_requests.clear()
    token_sources.clear()
    token_returns.clear()
    token_calls.clear()
    token_cursor_checks.clear()
    token_pending.clear(); token_checks.clear()
    section_entries.clear()
    section_pending.clear()
    section_returns.clear()
    copy_pending.clear()
    copy_results.clear()
    color_pending.clear(); color_entries.clear(); color_results.clear(); color_scan_results.clear()
    format_entries.clear()
    format_pending.clear(); format_results.clear()
    m.cpu.reg_write(UC_X86_REG_ECX,m.TYPE); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100b000))
    writes.clear(); active[0]=True
    m.cpu.emu_start(0x70ab40,0x103e000,count=1000000)
    active[0]=False
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x103e000 and m.cpu.reg_read(UC_X86_REG_EAX)&255==1
    assert not writes and m.cpu.mem_read(m.TYPE+0x205,1)==bytes([initial])
    if connected and mode==3:
        assert m.cpu.mem_read(m.TYPE+0xaf3,1)==b'\x01' and m.cpu.mem_read(m.TYPE+0xaa0,1)==b'\x00'
        assert m.signed(m.TYPE+0x5a4)==(2 if speed_override else 1)
        if real_bool: assert len(bool_entries)>100
        if real_art:
            assert {(0x50f410,'Voxel'),(0x50f410,'Remapable'),(0x50e9a0,'Cameo')} <= set(art_entries)
            # Track the Cameo copy and original boolean reads separately.
            assert {'Voxel','Remapable','Cameo'} <= art_value_reads, art_value_reads
        if real_integer:
            assert {('MCV','Facings'),('AMCV','Strength')} <= set(integer_entries)
            assert integer_checked=={'AMCV:Strength','AMCV:Sight','AMCV:TechLevel','AMCV:Speed','AMCV:Cost','MCV:Facings'} and not integer_pending
        if real_float:
            assert ('AMCV','Weight') in float_entries and set(float_conversions)=={'3.5','6','1'}
            assert struct.unpack('<d',bytes(m.cpu.mem_read(m.TYPE+0x340,8)))[0]==3.5
            assert ('AMCV','AccelerationFactor') in float_entries
            assert bytes(m.cpu.mem_read(m.TYPE+0x2d8,8))==constructor[0x2d8:0x2e0]
        if real_speed:
            # TechnoType reads first; UnitType then applies the Crusher default.
            assert speed_entries==[('AMCV','SpeedType',0xffffffff),('AMCV','SpeedType',2 if speed_override else 1)]
            if speed_override:
                assert raw_rules['SpeedType']=='Wheel' and len(speed_copies)==2
            else:
                assert 'SpeedType' not in raw_rules
            assert raw_rules['MovementZone']==('Subterannean' if zone_override else 'Normal')
            assert zone_entries==[('AMCV','MovementZone',0)] and len(zone_copies)==1
            assert m.signed(m.TYPE+0x500)==(6 if zone_override else 0)
            assert m.cpu.mem_read(m.TYPE+0xaf5,1)==bytes([int(zone_override)])
        if real_restricted:
            assert restricted_entries==[('AMCV','MovementRestrictedTo',0xffffffff)]
            assert m.signed(m.TYPE+0xb94)==(2 if restricted_override else -1)
            assert len(restricted_copies)==int(restricted_override)
        if real_armor:
            assert raw_rules['Armor']=='heavy' and armor_entries==[('AMCV','Armor',0)]
            assert len(armor_copies)==1 and armor_returns==[5] and m.signed(m.TYPE+0x9c)==5
        if real_vhpscan:
            assert vhpscan_entries==[('AMCV','VHPScan',0)]
            assert vhpscan_tokens==[b'Strong' if vhpscan_override else b'None']
            assert len(vhpscan_copies)==int(vhpscan_override)
            assert vhpscan_returns==[2 if vhpscan_override else 0]
            assert m.signed(m.TYPE+0x364)==(2 if vhpscan_override else 0)
        if real_compare:
            assert ('heavy','heavy') in compare_entries
            assert (('strong','strong') if vhpscan_override else ('none','none')) in compare_entries
            assert not compare_pending and len(compare_results)==len(compare_entries)
        if real_token:
            assert len(thread_requests)==(len(token_calls) if real_tokens else 1) and len(token_sources)==1
            source,length=token_sources[0]
            assert token_returns==[source] and token_cursor_checks==[True]
            if real_tokens: assert len(token_checks)==len(token_calls)==29 and not token_pending
        if real_section:
            assert section_entries==['AMCV'] and section_returns==[section] and not section_pending
        if real_copy: assert len(copy_results)==(96 if real_tokens else 89 if real_color else 87) and not copy_pending
        if real_color:
            assert color_entries==['RadialColor','LineTrailColor']
            assert color_results==[b'\0\0\0',b'\x80\x80\x80']
            assert color_scan_results==[(0,0,0),(128,128,128)] and not color_pending
        if real_format:
            assert format_entries==['%d,%d,%d']*2+['FiringSyncFrame%d']*2+['BurstDelay%d']*4
            assert len(format_results)==8 and not format_pending
    assert calls['parent']==1 and calls['pair']>=1 and calls['format']>=6 and calls['compare']>=1
    if connected: assert chain_calls==[0x70ab40,0x6daf60,0x5d7810,0x4107e0]
parent_ok[0]=False
if connected: base['parent_result'][0]=0
if real_section:
    mode=-1
    m.bind(0x50c9b0,4,base['section_lookup']); m.cpu.ctl_remove_cache(0x50c9b0,0x50c9b3)
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
if real_section:
    # A known empty supplied section registry exercises the original failure path.
    mode=3
    m.cpu.mem_write(0x50c9b0,section_prologue); m.callbacks.pop(0x50c9b0,None)
    m.cpu.ctl_remove_cache(0x50c9b0,0x50c9b3)
    m.word(bool_ini+0x24,0)
    m.cpu.mem_write(m.TYPE,constructor); m.cpu.mem_write(m.TYPE+0x205,b'\x01')
    m.cpu.mem_write(m.TYPE+0x24,b'AMCV\0')
    calls.update(parent=0,pair=0,format=0,compare=0)
    chain_calls.clear(); section_entries.clear(); section_pending.clear(); section_returns.clear()
    m.cpu.reg_write(UC_X86_REG_ECX,m.TYPE); m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100b000))
    writes.clear(); active[0]=True
    m.cpu.emu_start(0x70ab40,0x103e000,count=1000000)
    active[0]=False
    assert m.cpu.reg_read(UC_X86_REG_EIP)==0x103e000 and m.cpu.reg_read(UC_X86_REG_EAX)&255==0
    assert section_entries==['AMCV'] and section_returns==[0] and not section_pending
    assert not writes and m.cpu.mem_read(m.TYPE+0x205,1)==b'\x01'
    assert calls=={'parent':1,'pair':0,'format':0,'compare':0}
    assert chain_calls==[0x70ab40,0x6daf60,0x5d7810,0x4107e0]
print('base x86 %sreader chain preserves supplied Type +205 in %d checked cases'%
      ('connected real-format ' if real_format else 'connected real-tokens ' if real_tokens else 'connected real-color ' if real_color else 'connected real-copy ' if real_copy else 'connected real-section ' if real_section else 'connected token-Strong ' if real_token and vhpscan_override else 'connected real-token ' if real_token else 'connected compare-Strong ' if real_compare and vhpscan_override else 'connected real-compare ' if real_compare else 'connected synthetic-VHPScan ' if vhpscan_override else 'connected real-VHPScan ' if real_vhpscan else 'connected real-armor ' if real_armor else 'connected synthetic-Water ' if restricted_override else 'connected real-placement ' if real_restricted else 'connected synthetic-zone ' if zone_override else 'connected synthetic-Wheel ' if speed_override else 'connected real-speed ' if real_speed else 'connected real-float ' if real_float else 'connected real-integer ' if real_integer else 'connected real-art ' if real_art else 'connected real-boolean ' if real_bool else 'connected ' if connected else 'UnitType ',10 if real_section else 9 if connected else 7))
