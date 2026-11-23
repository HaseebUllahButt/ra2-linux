#!/usr/bin/env python3
"""Function checks. Generated game-derived C stays under build/xverify."""
import sys, os, json, random, subprocess, concurrent.futures, hashlib, re
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
REF=ROOT.parent/'ref-community/sp00nznet_pcrecomp'
GAME=ROOT.parent/'ra2-1to1/inputs/game.exe'
OUT=ROOT/'build/xverify'
sys.path[:0]=[str(ROOT/'tools'),str(REF/'tools/lift')]
import lift as ours
import generate as gen
from lift32 import Lifter
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_IMM

def run(cmd, log):
    p=subprocess.run([str(x) for x in cmd],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    Path(log).write_bytes(p.stdout)
    return p.returncode

def prepare():
    OUT.mkdir(exist_ok=True); (OUT/'.trackerignore').touch()
    for p in OUT.glob("run_*.log"):
        p.unlink()
    for p in OUT.glob("[ot]_*.c"):
        p.unlink()
    img=ours.Image(str(GAME)); md=Cs(CS_ARCH_X86,CS_MODE_32); md.detail=True
    entries=set(int(x,16) for x in Path(ours.ENTRIES).read_text().split())
    funcs={}; meta={}
    for a in sorted(entries):
        f=ours.Func(img,md,a,entries)
        try:
            f.discover(); ins=list(f.insns.values())
            depth=need=0
            for i in sorted(ins,key=lambda i:i.address):
                m=i.mnemonic
                if not m.startswith('f') or m in ('fnstcw','fstcw','fldcw','fnstsw','fstsw','fnclex','fwait','wait'): continue
                req=0 if m.startswith(('fld','fild')) and 'st(' not in i.op_str else 1
                for st in re.findall(r'st\((\d)\)', i.op_str): req=max(req,int(st)+1)
                if m in ('fpatan','fyl2x','fyl2xp1','faddp','fmulp','fsubp','fsubrp','fdivp','fdivrp','fcompp','fucompp'): req=max(req,2)
                need=max(need,req-depth)
                if m.startswith(('fld','fild')) and m!='fldcw': depth+=1
                if m in ('fstp','fistp','fisttp','faddp','fmulp','fsubp','fsubrp','fdivp','fdivrp','fpatan','fyl2x','fyl2xp1','fcomp','fucomp'): depth-=1
                if m in ('fcompp','fucompp'): depth-=2
            fp=any(i.mnemonic.startswith('f') and i.mnemonic not in ('fs','fwait') for i in ins)
            unsafe=[]
            if f.bad or not ins: unsafe.append('incomplete decode')
            if len(ins)>400: unsafe.append('more than 400 instructions')
            if any(any(s in i.op_str for s in ('fs:','gs:')) or i.mnemonic in ('rdtsc','cpuid','int','int3','hlt','ud2','sysenter','in','out') for i in ins): unsafe.append('host or system state')
            calls=set()
            for i in ins:
                if i.mnemonic=='call' or (i.mnemonic=='jmp' and (not i.operands or i.operands[0].type!=X86_OP_IMM or i.operands[0].imm not in f.insns)):
                    if i.operands and i.operands[0].type==X86_OP_IMM: calls.add(i.operands[0].imm)
                    else: unsafe.append('indirect call or jump')
            if not unsafe: funcs[a]=f
            meta[a]=dict(addr=f'{a:08x}',x87=fp,nfin=min(4,max(0,need)),insns=len(ins),calls=sorted(calls),unsafe=unsafe)
        except Exception as e: meta[a]=dict(addr=f'{a:08x}',x87=False,unsafe=[str(e)],calls=[])
    def closure(a,seen=None):
        seen=set() if seen is None else seen
        if a in seen: return seen
        if a not in meta or meta[a]['unsafe']: raise ValueError('unsafe closure')
        seen.add(a)
        if len(seen)>8: raise ValueError('closure exceeds eight functions')
        for c in meta[a]['calls']: closure(c,seen)
        return seen
    eligible={}
    for a in meta:
        try: eligible[a]=closure(a)
        except ValueError: pass
    fp=[a for a in meta if meta[a]['x87']]
    other=[a for a in eligible if not meta[a]['x87']]
    sample=random.Random(0x58564552).sample(sorted(other),1000)
    selected=sorted(set(fp+sample)); needed=set().union(*(eligible[a] for a in selected if a in eligible))
    manifest=dict(seed=0x58564552,states=200,game_sha256=hashlib.sha256(GAME.read_bytes()).hexdigest(),catalog=len(meta),x87_total=len(fp),eligible=len(eligible),sample_frame=len(other),functions=[])
    for a in selected:
        manifest['functions'].append(dict(meta[a],set='x87' if a in fp else 'sample',eligible=a in eligible))
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2))
    for a in sorted(needed):
        f=funcs[a]; row=meta[a]
        try:
            src,_=ours.lift(img,md,a,entries); (OUT/f'o_{a:08x}.c').write_text(src)
        except Exception as e: row['our_error']=str(e)
        try:
            ins=[gen.LinearInstruction(f.insns[x]) for x in sorted(f.insns)]
            src=gen.lift_function_linear(Lifter(iat_map={},lifted=set()),f'sub_{a:08X}',ins,set(f.targets)|{a},a)
            (OUT/f't_{a:08x}.c').write_text('#define RECOMP_GENERATED_CODE\n#include "recomp_types.h"\n'+src)
        except Exception as e: row['their_error']=str(e)
    (OUT/'catalog.json').write_text(json.dumps({f'{a:08x}':r for a,r in meta.items()},indent=2))
    print(f'Catalog {len(meta)}; x87 {len(fp)}; sample frame {len(other)}; emitted {len(needed)}',flush=True)

def extend_x87():
    from extend_x87 import extend
    extend(sys.modules[__name__])

def calibrate():
    from stack_inputs import infer
    m=json.loads((OUT/'manifest.json').read_text())
    img=ours.Image(str(GAME));md=Cs(CS_ARCH_X86,CS_MODE_32);md.detail=True
    entries=set(int(x,16) for x in Path(ours.ENTRIES).read_text().split())
    r=infer(img,md,entries,[int(f['addr'],16) for f in m['functions'] if f['eligible']],ours.Func)
    for f in m['functions']:
        if not f['eligible']:continue
        v=r[int(f['addr'],16)]
        changed='error' in v or v['nfin']!=f.get('nfin',0)
        if 'error' in v:f['eligible']=False;f['stack_error']=v['error']
        else:f['nfin']=v['nfin']
        if changed:
            log=OUT/('run_'+f['addr']+'.log')
            if log.exists():log.unlink()
    (OUT/'manifest.json').write_text(json.dumps(m,indent=2))
    (OUT/'stack_inputs.json').write_text(json.dumps({f'{a:08x}':v for a,v in r.items()},indent=2))
    print('Validated x87 input depths',flush=True)

def build():
    flags=['gcc','-m32','-std=gnu11','-O1','-g','-ffunction-sections','-fdata-sections','-fno-strict-aliasing','-fwrapv','-w','-fno-pie','-no-pie','-mfpmath=387','-frounding-math','-DRECOMP_FLAT_MEMORY','-DRECOMP_LOCAL_REGS','-I'+str(ROOT/'rt'),'-I'+str(REF/'runtime/recomp32'),'-I'+str(ROOT.parent/'ra2-1to1/src')]
    def compile(p):
        o=p.with_suffix('.o')
        if o.exists() and o.stat().st_mtime >= p.stat().st_mtime: return str(p),0
        rc=run(flags+['-c',p,'-o',o],p.with_suffix('.log'))
        return str(p),rc
    paths=sorted(OUT.glob('[ot]_*.c'))
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool: results=list(pool.map(compile,paths))
    good={Path(p).stem for p,r in results if r==0}
    (OUT/'compile.json').write_text(json.dumps(results))
    addrs=sorted(set(int(s[2:],16) for s in good))
    text=['#include <stdint.h>','struct testfn { uint32_t addr; void (*ours)(void), (*theirs)(void); };']
    for a in addrs:
        if f'o_{a:08x}' in good: text.append(f'void f_{a:08x}(void);')
        if f't_{a:08x}' in good: text.append(f'void sub_{a:08X}(void);')
    text.append('const struct testfn tests[] = {')
    for a in addrs:
        o=f'f_{a:08x}' if f'o_{a:08x}' in good else '0'; t=f'sub_{a:08X}' if f't_{a:08x}' in good else '0'
        text.append(f'{{0x{a:08x}, {o}, {t}}},')
    text+=['{0,0,0}};','struct rc_fn {uint32_t a; void (*f)(void);};','const struct rc_fn rc_table[]={{0,0}}; const int rc_table_n=0;']
    (OUT/'table.c').write_text('\n'.join(text))
    T=ROOT.parent/'ra2-1to1'
    sources=[ROOT/'tools/xverify/harness.c',ROOT/'tools/xverify/their_rt.c',ROOT/'rt/rc_rt.c',ROOT/'rt/bridge.S',T/'runtime/pilot_rt.c',OUT/'table.c']
    objs=[OUT/(s+'.o') for s in sorted(good)]
    rc=run(flags+sources+objs+['-Wl,--gc-sections','-lm','-o',OUT/'check'],OUT/'link.log')
    if rc: print((OUT/'link.log').read_text()[-6000:]); raise SystemExit('link failed')
    print(f'Compiled {len(good)}/{len(paths)} bodies',flush=True)

def execute():
    identity=hashlib.sha256((OUT/'check').read_bytes()+(OUT/'manifest.json').read_bytes()).hexdigest()
    marker=OUT/'run_identity.txt'
    if not marker.exists() or marker.read_text()!=identity:
        for p in OUT.glob('run_*.log'):p.unlink()
        marker.write_text(identity)
    m=json.loads((OUT/'manifest.json').read_text())
    def work(f):
        a=f['addr']
        if not f['eligible']: return dict(addr=a,set=f['set'],cannot=f.get('stack_error','unsafe closure'),ours=None,theirs=None)
        log=OUT/f'run_{a}.log'
        if log.exists():
            for line in log.read_text().splitlines():
                if line.startswith('{"addr":'):
                    r=json.loads(line);r['set']=f['set'];return r
        try:
            p=subprocess.run([str(OUT/'check'),str(GAME),a,str(f.get('nfin',0))],cwd=ROOT,capture_output=True,text=True,timeout=1800)
        except subprocess.TimeoutExpired:
            return dict(addr=a,set=f['set'],cannot='worker timeout',ours=None,theirs=None)
        (OUT/f'run_{a}.log').write_text(p.stdout+p.stderr)
        try: r=json.loads(p.stdout.splitlines()[-1]); r['set']=f['set']; return r
        except Exception: return dict(addr=a,set=f['set'],cannot=f'worker exit {p.returncode}',ours=None,theirs=None)
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        results=list(pool.map(work,m['functions']))
    (OUT/'results.json').write_text(json.dumps(results,indent=2))
    for group in ('x87','sample'):
        for side in ('ours','theirs'):
            counts={k:0 for k in ('exact','differ','same fault','cannot test')}
            for r in results:
                if r['set']!=group: continue
                s=r.get(side)
                status='cannot test' if not s else 'differ' if s['differ'] else 'same fault' if s['fault'] else 'cannot test' if s['cannot'] else 'exact'
                counts[status]+=1
            print(group,side,counts,flush=True)
if __name__=='__main__':
    for step in sys.argv[1:] or ['prepare','extend_x87','calibrate','build','execute']: globals()[step]()
