"""Include all bounded static x87 call closures, without the sample's size limit."""
import json
from pathlib import Path

def extend(d):
    out=d.OUT;m=json.loads((out/'manifest.json').read_text())
    img=d.ours.Image(str(d.GAME));md=d.Cs(d.CS_ARCH_X86,d.CS_MODE_32);md.detail=True
    entries=set(int(x,16) for x in Path(d.ours.ENTRIES).read_text().split())
    cache={};needed=set();added=0
    def inspect(a):
        if a in cache:return cache[a]
        if a not in entries:raise ValueError('unknown callee')
        f=d.ours.Func(img,md,a,entries);f.discover();calls=set()
        if f.bad or not f.insns:raise ValueError('incomplete decode')
        for i in f.insns.values():
            if any(s in i.op_str for s in ('fs:','gs:')) or i.mnemonic in ('rdtsc','cpuid','int','int3','hlt','ud2','sysenter','in','out'):raise ValueError('host or system state')
            if i.mnemonic=='call' or (i.mnemonic=='jmp' and (i.operands[0].type!=d.X86_OP_IMM or i.operands[0].imm not in f.insns)):
                if i.operands[0].type==d.X86_OP_IMM:calls.add(i.operands[0].imm)
                elif i.mnemonic=='jmp' and i.address in f.tables and f.tables[i.address]:pass
                else:raise ValueError('unknown indirect call or jump')
        cache[a]=(f,calls);return cache[a]
    def closure(a,seen):
        if a in seen:return
        seen.add(a)
        if len(seen)>8:raise ValueError('closure exceeds eight functions')
        f,calls=inspect(a)
        for c in sorted(calls):closure(c,seen)
    for row in m['functions']:
        if row['set']!='x87' or row['eligible']:continue
        try:
            seen=set();closure(int(row['addr'],16),seen)
            needed.update(seen);row['eligible']=True;row['extended_x87']=True;added+=1
        except (ValueError,d.ours.LiftError) as e:row['extension_error']=str(e)
    for a in sorted(needed):
        f,_=inspect(a)
        if not (out/f'o_{a:08x}.c').exists():
            try:
                src,_=d.ours.lift(img,md,a,entries);(out/f'o_{a:08x}.c').write_text(src)
            except d.ours.LiftError as e:(out/f'o_{a:08x}.error').write_text(str(e))
        if not (out/f't_{a:08x}.c').exists():
            ins=[d.gen.LinearInstruction(f.insns[x]) for x in sorted(f.insns)]
            src=d.gen.lift_function_linear(d.Lifter(iat_map={},lifted=set()),f'sub_{a:08X}',ins,set(f.targets)|{a},a)
            (out/f't_{a:08x}.c').write_text('#define RECOMP_GENERATED_CODE\n#include "recomp_types.h"\n'+src)
    (out/'manifest.json').write_text(json.dumps(m,indent=2))
    print(f'Added {added} x87 roots without a size limit',flush=True)
