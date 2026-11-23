#define _GNU_SOURCE
#include "win32hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
static FILE *trace; static int enabled=-1,scene=-1,base_frame; static uint32_t shooter,victim,types[2],house;
static const char *names[]={"gi-1","gi-2","gi-3","conscript-1","conscript-2","conscript-3"};
static uint32_t invoke(uint32_t obj,uint32_t fn,int n,const uint32_t *args){
 uint32_t save[]={g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi,g_cur_func,g_flag_k,g_flag_a,g_flag_b,g_flag_cf};
 uint32_t sp=(g_esp-0x800)&~15u; unsigned char backup[64];memcpy(backup,(void*)sp,64);
 g_esp=sp;g_ecx=obj;for(int i=n-1;i>=0;i--)PUSH32(g_esp,args[i]);PUSH32(g_esp,0);
 recomp_func_t f=recomp_lookup(fn);if(!f){fprintf(stderr,"[kill] missing %08x\n",fn);exit(90);}f();uint32_t out=g_eax;
 memcpy((void*)sp,backup,64);g_eax=save[0];g_ecx=save[1];g_edx=save[2];g_ebx=save[3];g_esp=save[4];g_ebp=save[5];g_esi=save[6];g_edi=save[7];g_cur_func=save[8];g_flag_k=save[9];g_flag_a=save[10];g_flag_b=save[11];g_flag_cf=save[12];return out;
}
static void discover(void){
 FILE *maps=fopen("/proc/self/maps","r");char line[512],perm[8];unsigned long lo,hi;
 while(fgets(line,sizeof line,maps)){
  if(sscanf(line,"%lx-%lx %7s",&lo,&hi,perm)!=3||perm[0]!='r'||perm[1]!='w'||hi>=0x80000000ul||hi-lo>512ul*1024*1024)continue;
  for(uintptr_t a=(lo+3)&~3u;a+0x260<hi;a+=4){
   uint32_t vt=*(uint32_t*)a;
   if(vt==0x7a3a78){if(!strcmp((char*)(a+0x24),"E1"))types[0]=a;if(!strcmp((char*)(a+0x24),"E2"))types[1]=a;}
   if(vt==0x7a2de0 && *(int*)(a+0x30)==0)house=a;
  }
 }
 fclose(maps);fprintf(stderr,"[kill] E1=%08x E2=%08x house=%08x\n",types[0],types[1],house);
 if(!types[0]||!types[1]||!house)exit(91);
}
static int position(uint32_t obj,int x,int y,int z){
 uint32_t vt=MEM32(obj);invoke(obj,MEM32(vt+0xd0),0,NULL);
 int xyz[]={x,y,z};uint32_t args[]={(uint32_t)(uintptr_t)xyz,0};uint32_t ok=invoke(obj,MEM32(vt+0xd4),2,args);
 fprintf(stderr,"[kill] Put %08x ok=%u xyz=%d,%d,%d\n",obj,ok,(int)MEM32(obj+0x88),(int)MEM32(obj+0x8c),(int)MEM32(obj+0x90));
 if(!(ok&255))return 0;
 int fields[][2]={{0x98,13},{0xa0,-1},{0x228,0},{0x494,0},{0x6c,125},{0x244,-1},{0x248,0},{0x24c,0},{0x2e0,0},{0x2e4,0},{0x2e8,-1},{0x2f0,0}};
 for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++)MEM32(obj+fields[i][0])=fields[i][1];MEM8(obj+0x57d)=0;MEM8(obj+0x5c3)=0;
 uint32_t zero[3]={0};invoke(obj,MEM32(vt+0x4dc),3,zero);return 1;
}
static void sample(uint32_t o,int frame){
 int start=MEM32(o+0x244),duration=MEM32(o+0x24c),left=start==-1?duration:duration-(frame-start);if(left<0)left=0;
 fprintf(trace,"{\"type\":\"%s\",\"hp\":%d,\"alive\":%u,\"xyz\":[%d,%d,%d],\"owner\":\"0x%x\",\"mission\":%d,\"target\":\"0x%x\",\"stage\":%d,\"sequence\":%d,\"firing\":%u,\"prone\":%u,\"facing\":%u,\"facing_start\":%u,\"rearm_start\":%d,\"rearm_duration\":%d,\"rearm_left\":%d}",
 (char*)(uintptr_t)(MEM32(o+0x5a8)+0x24),(int)MEM32(o+0x6c),MEM8(o+0x7d),(int)MEM32(o+0x88),(int)MEM32(o+0x8c),(int)MEM32(o+0x90),MEM32(o+0x1b4),(int)MEM32(o+0x98),MEM32(o+0x228),(int)MEM32(o+0xe0),(int)MEM32(o+0x5ac),MEM8(o+0x57d),MEM8(o+0x5c3),MEM16(o+0x2e0),MEM16(o+0x2e4),start,duration,left);
}
void join_kill_frame(void){
 if(enabled==-1)enabled=getenv("JOIN_KILL_OUT")!=NULL;if(!enabled)return;
 int frame=MEM32(0xa40d2c);if(frame<100)return;
 if(scene==-1){discover();scene=0;}
 if(!trace){
  uint32_t owner_arg[]={house};shooter=invoke(types[scene>=3],0x509ef0,1,owner_arg);victim=invoke(types[scene>=3],0x509ef0,1,owner_arg);
  if(!shooter||!victim)exit(93);
  int y=scene<2?20544:19520,z=scene<2?208:624,placed=0;
  for(int attempt=0;attempt<289&&!placed;attempt++){
   int dx=attempt?((attempt-1)%17-8)*256:0,dy=attempt?((attempt-1)/17-8)*256:0;
   int xyz[]={22464+dx,y+dy,z};uint32_t a[]={(uint32_t)(uintptr_t)xyz};int ground=invoke(0x8324e0,0x55a280,1,a);xyz[0]+=512;int ground2=invoke(0x8324e0,0x55a280,1,a);
   if(ground!=ground2)continue;
   int zz=attempt?ground:z;
   if(!position(shooter,22464+dx,y+dy,zz)||!position(victim,22976+dx,y+dy,zz))continue;
   placed=MEM32(victim+0x88)==MEM32(shooter+0x88)+512 && MEM32(victim+0x8c)==MEM32(shooter+0x8c) && MEM32(victim+0x90)==MEM32(shooter+0x90);
  }
  if(!placed)exit(92);
  MEM32(victim+0x1b4)=MEM32(shooter+0x1b4);MEM32(shooter+0x98)=1;MEM32(shooter+0x228)=victim;base_frame=frame;
  char path[1024];snprintf(path,sizeof path,"%s/%s.jsonl",getenv("JOIN_KILL_OUT"),names[scene]);trace=fopen(path,"w");if(!trace)exit(94);
  fprintf(trace,"{\"meta\":{\"label\":\"%s\",\"base_frame\":%d,\"shooter\":\"0x%x\",\"target\":\"0x%x\",\"method\":\"native hook at 0054068a; Limbo/Put; same-house Attack\"}}\n",names[scene],frame,shooter,victim);
 }
 fprintf(trace,"{\"frame\":%d,\"game_frame\":%d,\"shooter\":",frame-base_frame,frame);sample(shooter,frame);fputs(",\"target\":",trace);sample(victim,frame);fputs("}\n",trace);fflush(trace);
 if((int)MEM32(victim+0x6c)<=0||frame-base_frame>=1000){fprintf(stderr,"[kill] %s ends frame=%d hp=%d\n",names[scene],frame-base_frame,(int)MEM32(victim+0x6c));fclose(trace);trace=NULL;MEM32(shooter+0x98)=13;MEM32(shooter+0x228)=0;invoke(shooter,MEM32(MEM32(shooter)+0xd0),0,NULL);if((int)MEM32(victim+0x6c)>0 && MEM8(victim+0x7d)==1)invoke(victim,MEM32(MEM32(victim)+0xd0),0,NULL);if(++scene==6){fprintf(stderr,"[kill] all scenes finished\n");exit(0);}}
}
