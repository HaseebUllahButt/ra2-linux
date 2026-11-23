"""Find a valid initial x87 depth across known control-flow paths and calls."""
import re

def infer(img, md, entries, addresses, Func):
    cache={}; active=set()
    def summary(addr):
        if addr in cache:return cache[addr]
        if addr in active:raise ValueError('recursive x87 depth')
        active.add(addr)
        f=Func(img,md,addr,entries);f.discover()
        todo=[(addr,0)];seen=set();need=peak=0;ends=set()
        while todo:
            a,d=todo.pop()
            if (a,d) in seen:continue
            seen.add((a,d))
            if not -8<=d<=8:raise ValueError('unbounded x87 depth')
            i=f.insns[a];m=i.mnemonic;req=delta=0
            if m.startswith('f') and m not in ('fnstcw','fstcw','fldcw','fnstsw','fstsw','fnclex','fclex','fwait','fxam','wait'):
                if m in ('fninit','finit','fnsave','fsave','frstor','fldenv','fnstenv','fstenv','fincstp','fdecstp','ffree'):raise ValueError('x87 environment operation')
                req=0 if m.startswith(('fld','fild')) and 'st(' not in i.op_str else 1
                for st in re.findall(r'st\((\d)\)',i.op_str):req=max(req,int(st)+1)
                if m in ('fpatan','fyl2x','fyl2xp1','faddp','fmulp','fsubp','fsubrp','fdivp','fdivrp','fcompp','fucompp','fscale','fprem','fprem1'):req=max(req,2)
                if m.startswith(('fld','fild')) or m in ('fptan','fsincos','fxtract'):delta+=1
                if m in ('fstp','fistp','fisttp','faddp','fmulp','fsubp','fsubrp','fdivp','fdivrp','fpatan','fyl2x','fyl2xp1','fcomp','fucomp'):delta-=1
                if m in ('fcompp','fucompp'):delta-=2
            need=max(need,req-d);d+=delta;peak=max(peak,d)
            nxt=f.succ.get(a,[])
            if m=='call' or (m=='jmp' and not nxt):
                target=i.operands[0].imm
                cn,cp,ce=summary(target);need=max(need,cn-d);peak=max(peak,d+cp)
                ds={d+x for x in ce}
            else:ds={d}
            if not nxt:ends.update(ds)
            else:todo.extend((n,dd) for n in nxt for dd in ds)
        active.remove(addr)
        if need+peak>8:raise ValueError('no common valid x87 depth')
        cache[addr]=(max(0,need),peak,ends);return cache[addr]
    results={}
    for a in addresses:
        try:results[a]={'nfin':summary(a)[0]}
        except (ValueError,KeyError) as e:results[a]={'error':str(e)};active.clear()
    return results
