#!/usr/bin/env python3
"""Compile old/new exact x87 regions and compare payload, flags and memory bits.

Run with the project's capstone Python: python tests/x87_batch_selftest.py OUT.
The generated C and executable remain in OUT as reproducible evidence.
"""
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/lift'))
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from lift32 import Lifter
from x87_batch import batch_exact_x87

# Every trace has at least two instructions. Arbitrary initial stacks exercise
# overflow, underflow, empty tags, special values and all physical TOP values.
# Operand address 0x1000000 maps into an isolated byte array in this test.
A = bytes.fromhex('00000001')
traces = []
def trace(*hexes):
    traces.append(b''.join(bytes.fromhex(h) for h in hexes))
for op in ('d8c1','d8c9','d8e1','d8e9','d8f1','d8f9',
           'dec1','dec9','dee1','dee9','def1','def9',
           'd9fa','d9fe','d9ff','d9f0','d9f1','d9f2','d9f3',
           'd9f4','d9f5','d9f8','d9f9','d9fc','d9fd',
           'd9e0','d9e1','d9e4','d9e5','d9c9','ddc1','ddd9',
           'd8d1','d8d9','ded9','dde1','dde9','dad9',
           'dbf1','dbf9','dff1','dff9','dac1','dac9','dad1','dad9',
           'dbc1','dbc9','dbd1','dbd9','d9f6','d9f7','dbE2','9b'):
    trace('d9e8',op,'dfe0','ddd8')
for opcode in ('d9','dd','db','df'):
    for extension in range(8):
        # Valid memory arithmetic/load/store opcodes, with safe buffer sizes.
        code = bytes([int(opcode,16), extension * 8 + 5]) + A
        md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail=True
        ins=list(md.disasm(code,0x401000))
        if not ins or ins[0].mnemonic in ('fldenv','frstor','fnsave','fnstenv','fldcw','fbstp','fbld'):
            continue
        traces.append(bytes.fromhex('d9e8')+code+bytes.fromhex('dfe0ddd8'))
# Rounded stores, integer conversion and change-control barriers.
for op in ('d91d','dd1d','db3d','df3d','df1d','db1d','dd0d'):
    traces.append(bytes.fromhex('d9e8d9fcd9e8dec1')+bytes.fromhex(op)+A)
traces.append(bytes.fromhex('d9e8d92d')+A+bytes.fromhex('d9e8dec1dfe0'))
# Real hot helper's 3-vector dot product shape, with integer work inside.
traces.append(bytes.fromhex('d905')+A+bytes.fromhex('d80d')+A+
              bytes.fromhex('d905')+A+bytes.fromhex('d80d')+A+
              bytes.fromhex('dec1d905')+A+bytes.fromhex('d80d')+A+
              bytes.fromhex('dec1d91d')+A)

md=Cs(CS_ARCH_X86,CS_MODE_32);md.detail=True
functions=[]; batched=0
for n, raw in enumerate(traces):
    lifter=Lifter(iat_map={}, precise_carry=True, precise_sbb=True)
    body=''.join('    '+line+'\n' for ins in md.disasm(raw,0x401000)
                 for line in lifter.lift_instruction(ins))
    # An integer load/store between FP instructions must keep the region open.
    body=body.replace('    }\n','    }\n    eax ^= 0x12345678u; MEM32(0x1000040) = eax;\n')
    old='void old_%d(void) {\n%s}\n'%(n,body)
    new=batch_exact_x87(old.replace('old_%d'%n,'new_%d'%n))
    batched+=new.count('/* exact x87 region */')
    functions.extend((old,new))

# Each unknown line, label and control flow must end the region.
sample=''.join('    '+line+'\n' for ins in md.disasm(bytes.fromhex('d9e8d9e8'),0x401000)
               for line in Lifter(iat_map={}).lift_instruction(ins))
first=sample.index('    }\n')+6
for barrier in ('L_00401000:\n','    callback();\n','    if (eax) goto L_00401000;\n',
                '    double v = 1.0;\n','    RECOMP_BACKEDGE();\n','    _ST(0) = 1;\n'):
    assert '/* exact x87 region */' not in batch_exact_x87(sample[:first]+barrier+sample[first:]),barrier
assert batch_exact_x87(batch_exact_x87(sample)) == batch_exact_x87(sample)

out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
source=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint16_t g_fpu_cw;
long double g_st[8];
int g_fp_top;
#define RECOMP_SHARED_DATA
#include "x87_exact.h"
static unsigned char mem[256];
#define ADDR(a) (mem + ((uint32_t)(a)-0x1000000u))
#define MEM32(a) (*(uint32_t*)ADDR(a))
static uint32_t eax, _flag_a, _flag_b, _flag_k, _cf;
#define FK_EFLAGS 1
static uint32_t recomp_eflags(uint32_t k,uint32_t a,uint32_t b,uint32_t c,int d) { (void)k;(void)b;(void)c;(void)d;return a; }
static int _df=1;
'''+''.join(functions)+r'''
static uint64_t rng=0x43796c6573506572ull;
static uint64_t random64(void) { rng^=rng<<13; rng^=rng>>7; return rng^=rng<<17; }
struct snap { struct x87_image x; long double st[8]; unsigned char m[256]; uint16_t cw; int top; uint32_t a,fa,fb,fk,cf; };
static void take(struct snap *s) { memset(s,0,sizeof(*s));s->x=g_x87;memcpy(s->st,g_st,sizeof(g_st));memcpy(s->m,mem,256);s->cw=g_fpu_cw;s->top=g_fp_top;s->a=eax;s->fa=_flag_a;s->fb=_flag_b;s->fk=_flag_k;s->cf=_cf;
/* Code and data pointers describe the test functions' different locations. */
s->x.ip=s->x.dp=0;s->x.cs=s->x.opcode=s->x.ds=0;
/* Long double padding has no value and the old code never writes it. */
for(int j=0;j<8;j++)memset((unsigned char*)&s->st[j]+10,0,sizeof(long double)-10);
}
static void put(const struct snap *s) { g_x87=s->x;memcpy(g_st,s->st,sizeof(g_st));memcpy(mem,s->m,256);g_fpu_cw=s->cw;g_fp_top=s->top;eax=s->a;_flag_a=s->fa;_flag_b=s->fb;_flag_k=s->fk;_cf=s->cf; }
static const uint64_t edges[]={0,0x8000000000000000ull,1,0x8000000000000001ull,0xfffffffffffffull,0x10000000000000ull,0x7fefffffffffffffull,0xffefffffffffffffull,0x7ff0000000000000ull,0xfff0000000000000ull,0x7ff8000000000000ull,0x7ff0000000000001ull,0x3ff0000000000000ull,0xbff0000000000000ull};
static void (*oldfn[])(void)={OLD_LIST};
static void (*newfn[])(void)={NEW_LIST};
int main(void) {
struct snap init,a,b; unsigned long count=0;
for(unsigned cw=0;cw<32;cw++)for(unsigned input=0;input<512;input++) {
 memset(&init,0,sizeof init);
 init.x.cw=init.cw=0x7f|((cw&3)<<8)|(((cw>>2)&3)<<10)|((cw>>4)<<12);
 init.x.sw=((random64()&7)<<11)|(random64()&0x473f);init.x.tw=(uint16_t)random64();
 for(unsigned j=0;j<8;j++) {
   uint64_t m=input<14?edges[(input+j)%14]:random64();uint16_t e=(uint16_t)random64();
   if(input<32) { e=(uint16_t[]){0,0x8000,1,0x7ffe,0x7fff,0xffff,0x3fff,0x4000}[input%8];m= (uint64_t[]){0,1,0x8000000000000000ull,0xc000000000000000ull,0x8000000000000001ull,0xffffffffffffffffull}[j%6]; }
   memcpy(init.x.st[j],&m,8);memcpy(init.x.st[j]+8,&e,2);memcpy(&init.st[j],init.x.st[j],10);
 }
 for(unsigned j=0;j<256;j+=8){uint64_t v=input<14?edges[(input+j/8)%14]:random64();memcpy(init.m+j,&v,8);}
 /* fldcw trace must keep exceptions masked. */
 memcpy(init.m,&init.cw,2);
 init.a=(uint32_t)random64();init.fa=(uint32_t)random64()&0x45;init.cf=init.fa&1;
 for(unsigned f=0;f<sizeof(oldfn)/sizeof(*oldfn);f++) {
   put(&init);oldfn[f]();take(&a);put(&init);newfn[f]();take(&b);count++;
   if(memcmp(&a,&b,sizeof a)) { unsigned char *aa=(void*)&a,*bb=(void*)&b;for(unsigned k=0;k<sizeof a;k++)if(aa[k]!=bb[k]){fprintf(stderr,"FAIL trace=%u cw=%04x input=%u byte=%u old=%02x new=%02x\n",f,init.cw,input,k,aa[k],bb[k]);break;}return 1; }
 }
}
printf("PASS %lu old/new traces; %zu instruction streams; 32 control words; random and edge payloads, TOP and tags\n",count,sizeof(oldfn)/sizeof(*oldfn));return 0;
}
'''
source=source.replace('OLD_LIST',','.join('old_%d'%n for n in range(len(traces))))
source=source.replace('NEW_LIST',','.join('new_%d'%n for n in range(len(traces))))
(out/'x87-batch-test.c').write_text(source)
cmd=['gcc','-m32','-mfpmath=387','-frounding-math','-fno-strict-aliasing','-fwrapv','-O2','-g','-no-pie',
     '-I'+str(ROOT/'runtime/recomp32'),str(out/'x87-batch-test.c'),'-o',str(out/'x87-batch-test')]
subprocess.run(cmd,check=True)
r=subprocess.run([str(out/'x87-batch-test')],capture_output=True,text=True)
(out/'result.json').write_text(json.dumps({'compile':cmd,'traces':len(traces),'regions':batched,'exit':r.returncode,'stdout':r.stdout,'stderr':r.stderr},indent=2)+'\n')
print(r.stdout+r.stderr,end='');sys.exit(r.returncode)
