#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/time.h>
#include <sys/mman.h>
#include "rc.h"
int pilot_map_image(const char*,int);
void rc_run_native(u32); u32 rc_stack_top(void);
void their_in(void);void their_out(void);
struct testfn {u32 addr;void(*ours)(void),(*theirs)(void);};extern const struct testfn tests[];
static sigjmp_buf recovery;
static volatile int sig;static volatile u32 fault_addr;static volatile int fault_code;
#define DBASE 0x7c9000u
#define DSIZE (0xb2d000u-DBASE)
static unsigned char dirty[DSIZE/4096];
static void trap(int s,siginfo_t *i,void *v){
 u32 a=(u32)(uintptr_t)i->si_addr;
 if(s==SIGSEGV && i->si_code==SEGV_ACCERR && a>=DBASE && a<DBASE+DSIZE){
  u32 page=(a-DBASE)/4096;
  if(!dirty[page]){dirty[page]=1;if(!mprotect((void*)(DBASE+page*4096),4096,PROT_READ|PROT_WRITE|PROT_EXEC))return;}
 }
 sig=s;fault_addr=(s==SIGSEGV || s==SIGBUS)?(u32)(uintptr_t)i->si_addr:0;fault_code=i->si_code;siglongjmp(recovery,1);}
static unsigned char *scratch,*data0;static u32 rng;
static u32 rnd(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
#define SSIZE 0x10000
static uint64_t hash(const unsigned char *p,size_t n,uint64_t h){for(size_t i=0;i<n;i++){h^=p[i];h*=0x100000001b3ULL;}return h;}
struct result {u32 eax,edx,depth;unsigned char st[8][10];uint64_t scratch,data;int signal,code;u32 fault;};
static unsigned audit_count;
static void collect(struct result *r){
 if(!(audit_count++%64))for(u32 i=0;i<DSIZE;i+=4096)
  if(!dirty[i/4096] && memcmp((void*)(DBASE+i),data0+i,4096)){fprintf(stderr,"untracked data write at %08x\n",DBASE+i);_Exit(7);}

 r->eax=R.eax;r->edx=R.edx;
 for(int i=0;i<8 && (R.tag>>((R.top+i)&7)&1);i++){memcpy(r->st[i],&ST(i),10);r->depth++;}
 r->scratch=hash(scratch,SSIZE,1);r->data=1;
 for(u32 i=0;i<DSIZE;i+=4096) if(dirty[i/4096] && memcmp((void*)(DBASE+i),data0+i,4096)) {r->data=hash((unsigned char*)&i,4,r->data);r->data=hash((void*)(DBASE+i),4096,r->data);}
}
static void setup(u32 addr,int k,int cw,int nf){
 __asm__ volatile("fninit":::"memory");fldcw(cw?0x27f:0x37f);
 for(u32 i=0;i<DSIZE;i+=4096)if(dirty[i/4096]){memcpy((void*)(DBASE+i),data0+i,4096);mprotect((void*)(DBASE+i),4096,PROT_READ|PROT_EXEC);dirty[i/4096]=0;}
 rng=addr^(k+1)*0x9e3779b9u;
 memset(scratch,0,SSIZE);
 if(k%4) for(int i=0;i<SSIZE;i+=4){u32 n=rnd(),v=n%17;if(k%4==1){float f=((int)(n%20001)-10000)/37.0f;memcpy(&v,&f,4);}if(k%4==2)v=(u32)(uintptr_t)(scratch+0x4000+(n%128)*4);memcpy(scratch+i,&v,4);}
 u32 sp=(rc_stack_top()&~15u)-256;
 memset((void*)(sp-SSIZE),0,SSIZE+256);
 memset(&R,0,sizeof R);R.esp=sp;
 u32 *regs=&R.eax;
 for(int i=0;i<8;i++)if(i!=4)regs[i]=(u32)(uintptr_t)(scratch+0x4000+i*0x400);
 R.eax=k%3?rnd()%17:R.eax;R.edx=k%3?rnd()%17:R.edx;
 M32(sp)=0xc0dec0de;
 for(int i=1;i<=48;i++) M32(sp+4*i)=((k+i)%3)?(u32)(uintptr_t)(scratch+0x8000+i*64):rnd()%17;
 /* Exact double inputs make conversion to their double model lossless at entry. */
 for(int i=nf-1;i>=0;i--){double v=((int)(rnd()%20001)-10000)/37.0;fpush((f80)v);}
 rc_aux.host_esp=0;
 __asm__ volatile("fnclex":::"memory");
}
static void one(struct result *r,u32 addr,void(*fn)(void),int mode,int k,int cw,int nf){
 memset(r,0,sizeof *r);setup(addr,k,cw,nf);sig=0;fault_addr=0;fault_code=0;
 if(mode==2)their_in();
 __asm__ volatile("fninit":::"memory");
 fldcw(cw?0x27f:0x37f);
 if(!sigsetjmp(recovery,1)){
  struct itimerval t={{0,0},{0,20000}};setitimer(ITIMER_VIRTUAL,&t,0);
  if(mode==0)rc_run_native(addr);else fn();
 }
 struct itimerval t={{0,0},{0,0}};setitimer(ITIMER_VIRTUAL,&t,0);
 if(sig){r->signal=sig;r->code=fault_code;r->fault=sig==SIGVTALRM?0:fault_addr;__asm__ volatile("fninit":::"memory");}
 else {if(mode==2)their_out();collect(r);}
}
static void hex(const unsigned char*p){for(int i=9;i>=0;i--)printf("%02x",p[i]);}
int main(int argc,char**argv){
 setvbuf(stdout,0,_IONBF,0);
 if(argc<4)return 2;
 u32 addr=strtoul(argv[2],0,16);int nf=atoi(argv[3]);
 const struct testfn *f=tests;while(f->addr && f->addr!=addr)f++;
 if(!f->addr)return 3;
 if(pilot_map_image(argv[1],1))return 4;
 scratch=mmap((void*)0x20000000,SSIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 if(scratch!=(void*)0x20000000)return 5;
 data0=malloc(DSIZE);memcpy(data0,(void*)DBASE,DSIZE);
 if(mprotect((void*)DBASE,DSIZE,PROT_READ|PROT_EXEC))return 6;
 stack_t ss={.ss_sp=malloc(SIGSTKSZ*4),.ss_size=SIGSTKSZ*4};sigaltstack(&ss,0);
 struct sigaction sa={.sa_sigaction=trap,.sa_flags=SA_SIGINFO|SA_ONSTACK};sigemptyset(&sa.sa_mask);
 int signals[]={SIGSEGV,SIGBUS,SIGILL,SIGFPE,SIGVTALRM,SIGABRT};for(int i=0;i<6;i++)sigaction(signals[i],&sa,0);
 int extra[2][2]={{0}},pcs[2][2][4]={{{0}}};int stats[2][4]={{0}};int examples[2]={0};
 for(int cw=0;cw<2;cw++)for(int k=0;k<200;k++){
  struct result n,b;one(&n,addr,0,0,k,cw,nf);
  for(int s=0;s<2;s++){
   void(*fn)(void)=s?f->theirs:f->ours;if(!fn){stats[s][3]++;pcs[s][cw][3]++;continue;}
   one(&b,addr,fn,s+1,k,cw,nf);
   int category;
   if(n.signal==SIGVTALRM || b.signal==SIGVTALRM)category=3;
   else if(n.signal || b.signal)category=(n.signal==b.signal && n.code==b.code && n.fault==b.fault)?2:1;
   else category=memcmp(&n,&b,sizeof n)?1:0;
   stats[s][category]++;pcs[s][cw][category]++;
   if(category==1)extra[s][!!(n.signal||b.signal)]++;
   if(category==1 && examples[s]++<3){printf("EXAMPLE %s cw=%d k=%d native eax=%08x edx=%08x depth=%u st0=",s?"theirs":"ours",cw?53:64,k,n.eax,n.edx,n.depth);hex(n.st[0]);printf(" lifted eax=%08x edx=%08x depth=%u st0=",b.eax,b.edx,b.depth);hex(b.st[0]);printf(" mem=%d/%d fault=%d@%08x/%d@%08x\n",n.scratch!=b.scratch,n.data!=b.data,n.signal,n.fault,b.signal,b.fault);}
  }
 }
 printf("{\"addr\":\"%08x\",",addr);
 for(int s=0;s<2;s++){
  printf("\"%s\":{\"exact\":%d,\"differ\":%d,\"fault\":%d,\"cannot\":%d,\"return_differ\":%d,\"fault_differ\":%d",s?"theirs":"ours",stats[s][0],stats[s][1],stats[s][2],stats[s][3],extra[s][0],extra[s][1]);
  for(int cw=0;cw<2;cw++)printf(",\"pc%d\":{\"exact\":%d,\"differ\":%d,\"fault\":%d,\"cannot\":%d}",cw?53:64,pcs[s][cw][0],pcs[s][cw][1],pcs[s][cw][2],pcs[s][cw][3]);
  printf("}%s",s?"}\n":",");
 }

 return 0;
}
