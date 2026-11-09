#!/usr/bin/env python3
"""Check original base VehicleTypes enumeration against installed rules.

Original hash, section count, ordered-key, and string-reader bodies execute.
The fixture supplies the parsed section, its linked entries, and hash index.
This does not construct types or establish complete rules loading.
"""
import struct
import subprocess
import sys

from original_image import load_original
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP

if len(sys.argv)!=4:
    raise SystemExit('usage: original_vehicle_enumeration_check.py game.exe real_map_case game_dir')
executable,fixture,game=sys.argv[1:]
lines=subprocess.check_output([fixture,game,'--vehicle-all-entries'],text=True).splitlines()
entries=[tuple(line.split('\t',1)) for line in lines]
assert entries and all(len(row)==2 for row in entries) and entries[0]==('1','AMCV')
assert len(entries)<1024 and len({key.casefold() for key,_ in entries})==len(entries)
cpu=load_original(executable)
cpu.mem_map(0x1000000,0x200000)
ini,cache,section,array=0x1001000,0x1001100,0x1001200,0x1004000
section_name,hash_object,scratch=0x1003000,0x1003100,0x1003200
output,fallback=0x1002000,0x1002200
stack,stop=0x11ff000,0x11fe000

def word(address,value): cpu.mem_write(address,struct.pack('<I',value & 0xffffffff))
def read(address): return struct.unpack('<I',bytes(cpu.mem_read(address,4)))[0]
def call(address,receiver,args):
    cpu.reg_write(UC_X86_REG_ECX,receiver); cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.mem_write(stack,struct.pack('<'+'I'*(len(args)+1),stop,*args))
    cpu.emu_start(address,stop,count=1000000)
    assert cpu.reg_read(UC_X86_REG_EIP)==stop and cpu.reg_read(UC_X86_REG_ESP)==stack+4+len(args)*4
    return cpu.reg_read(UC_X86_REG_EAX)
def original_hash(text):
    raw=text.encode('ascii'); assert len(raw)<128
    cpu.mem_write(hash_object,bytes(16)); cpu.mem_write(scratch,raw+b'\0')
    return call(0x493fc0,hash_object,[scratch,len(raw)])
def string_at(address):
    assert address
    return bytes(cpu.mem_read(address,256)).split(b'\0',1)[0].decode('ascii')

cpu.mem_write(section_name,b'VehicleTypes\0'); cpu.mem_write(fallback,b'<missing>\0')
word(ini+0x20,cache); word(ini+0x24,1); word(ini+0x2c,1); word(ini+0x30,cache)
word(cache,original_hash('VehicleTypes')); word(cache+4,section)
word(section+0x2c,array); word(section+0x30,len(entries)); word(section+0x38,1)
nodes=0x1010000; cursor=0x1080000; indexed=[]
for index,(key,value) in enumerate(entries):
    key_raw=key.encode('ascii')+b'\0'; value_raw=value.encode('ascii')+b'\0'
    assert len(key_raw)<256 and len(value_raw)<256 and cursor+len(key_raw)+len(value_raw)<0x11f0000
    key_ptr=cursor; cpu.mem_write(cursor,key_raw); cursor+=len(key_raw)
    value_ptr=cursor; cpu.mem_write(cursor,value_raw); cursor+=len(value_raw)
    node=nodes+index*0x40
    word(node+4,nodes+((index+1)%len(entries))*0x40)
    word(node+8,nodes+((index-1)%len(entries))*0x40)
    word(node+0xc,key_ptr); word(node+0x10,value_ptr)
    indexed.append((original_hash(key),node))
word(section+0x18,nodes)
indexed.sort(key=lambda row:row[0] if row[0]<0x80000000 else row[0]-0x100000000)
assert len({digest for digest,_ in indexed})==len(indexed)
for index,(digest,node) in enumerate(indexed): word(array+8*index,digest); word(array+8*index+4,node)
assert call(0x50cb00,ini,[section_name])==len(entries)
for index,(key,value) in enumerate(entries):
    key_ptr=call(0x50ce00,ini,[section_name,index])
    assert string_at(key_ptr)==key
    cpu.mem_write(output,bytes(256))
    assert call(0x50e9a0,ini,[section_name,key_ptr,fallback,output,256])==len(value)
    assert string_at(output)==value
assert call(0x50ce00,ini,[section_name,len(entries)])==0
print('base x86 VehicleTypes count, order, and values match %d installed entries'%len(entries))
