#!/usr/bin/env python3
"""Check original hierarchy prefix writes and retained tails after failures.

Usage: original_hierarchy_memory_check.py executable [--base] [--vectors]
The original hierarchy builder and route search execute with existing bounded
allocation, usable-area and storage adapters. Count writes are observed directly.
"""
import struct
import sys
from unicorn import UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EAX
import original_hierarchy_route_check as g
cpu=g.cpu; r=g.r; active=False; written=0
def writes(cpu,access,address,size,value,user):
    global written
    if active and g.SEARCH+0xc74<=address<g.SEARCH+0xc80:
        assert size==4 and (address-g.SEARCH-0xc74)%4==0
        written|=1<<((address-g.SEARCH-0xc74)//4)
cpu.hook_add(UC_HOOK_MEM_WRITE,writes)
total=2166136261; failed_lower=0
for seed in range(32):
    g.b.run(seed); g.h.stride,grid=r.inputs(seed)
    h=2166136261
    for zone in (0,2,9):
        for source,destination in g.endpoints(seed,grid):
            words=[i+33 for i in range(1500)]; counts=[8,9,10]
            written=0; active=True
            g.run_search(seed,zone,source,destination,(words,counts))
            active=False
            found=cpu.reg_read(UC_X86_REG_EAX)&255
            if not found and written&4 and written!=7: failed_lower+=1
            def mix(value):
                global h
                h=((h^(value&0xffffffff))*16777619)&0xffffffff
            mix(found); mix(written); mix(r.signed(g.SEARCH+0x6c)); mix(r.signed(g.SEARCH+0x70))
            for value in struct.unpack('<3I',cpu.mem_read(g.SEARCH+0xc74,12)): mix(value)
            for value in struct.unpack('<1500H',cpu.mem_read(g.SEARCH+0xbc,3000)): mix(value)
    total=((total^h)*16777619)&0xffffffff
    if '--vectors' in sys.argv: print(1 if r.base else 2,seed,f'{h:08x}')
assert failed_lower,'original hierarchy fixture missed failed lower levels'
print('384 original hierarchy memory cases',f'{total:08x}','failed lower levels',failed_lower,file=sys.stderr)
