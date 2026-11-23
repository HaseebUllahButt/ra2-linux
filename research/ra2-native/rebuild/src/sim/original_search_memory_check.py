#!/usr/bin/env python3
"""Check original path-buffer retention and generated-node progress updates.

Usage: original_search_memory_check.py executable [--base] [--vectors]
The original regular search, reset, full Unit entry and postprocessing execute.
"""
import struct
import sys
import original_path_check as p
m=p.m; cpu=p.cpu; SEARCH=p.SEARCH
assert m.signed(SEARCH+0x6c)==-1 and m.read(SEARCH+0x70)==0
assert bytes(cpu.mem_read(SEARCH+0xbc,3012))==bytes(3012),'original constructor retained fields differ'
# Reset preserves the path words, counts, and both progress fields.
cpu.mem_write(SEARCH+0xbc,struct.pack('<1500H',*[i%65536 for i in range(1500)]))
cpu.mem_write(SEARCH+0xc74,struct.pack('<3I',8,9,10))
m.word(SEARCH+0x6c,17); m.word(SEARCH+0x70,123|(456<<16))
before=bytes(cpu.mem_read(SEARCH+0x6c,8))+bytes(cpu.mem_read(SEARCH+0xbc,3012))
p.invoke(0x427e90 if p.base else 0x42a5b0,SEARCH)
assert before==bytes(cpu.mem_read(SEARCH+0x6c,8))+bytes(cpu.mem_read(SEARCH+0xbc,3012)),'original reset cleared retained fields'
number=0; total=2166136261
for pattern in range(4):
    for profile in (None,20,21,23,24,26):
        for goal in ((56,67),(52,67),(54,67),(58,69),(54,69)):
            words=[0 if pattern==0 else 1 if pattern==1 else (1 if i%2 else 2) if pattern==2 else 2 for i in range(1500)]
            if pattern==3: words[0]=1
            counts=[2 if pattern else 0,3,4]
            result=p.run(goal,hierarchy_profile=profile,retained_paths=(words,counts))
            h=2166136261
            def mix(value):
                global h
                h=((h^(value&0xffffffff))*16777619)&0xffffffff
            mix(m.read(SEARCH+0x6c)); mix(m.read(SEARCH+0x70))
            for value in struct.unpack('<3I',cpu.mem_read(SEARCH+0xc74,12)): mix(value)
            for value in struct.unpack('<1500H',cpu.mem_read(SEARCH+0xbc,3000)): mix(value)
            count,directions,cost=result or (0,(),0)
            mix(count)
            for value in directions: mix(value)
            mix(cost)
            if '--vectors' in sys.argv: print(1 if p.base else 2,number,f'{h:08x}')
            number+=1; total=((total^h)*16777619)&0xffffffff
print('120 original retained search vectors',f'{total:08x}',file=sys.stderr)
