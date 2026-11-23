/* Adapter only. The reference header and emitted function bodies stay unchanged. */
#define mmx_pmulhw rc_mmx_pmulhw
#define mmx_pmullw rc_mmx_pmullw
#define mmx_punpckhwd rc_mmx_punpckhwd
#define mmx_punpcklbw rc_mmx_punpcklbw
#define mmx_psrlw rc_mmx_psrlw
#define mmx_psrad rc_mmx_psrad
#define mmx_packssdw rc_mmx_packssdw
#define mmx_packuswb rc_mmx_packuswb
#define mmx_paddusb rc_mmx_paddusb
#include "rc.h"
#undef mmx_pmulhw
#undef mmx_pmullw
#undef mmx_punpckhwd
#undef mmx_punpcklbw
#undef mmx_psrlw
#undef mmx_psrad
#undef mmx_packssdw
#undef mmx_packuswb
#undef mmx_paddusb
#include "recomp_types.h"
#undef eax
#undef ecx
#undef edx
#undef ebx
#undef esp
#undef ebp
#undef esi
#undef edi
uint32_t g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi,g_cur_func;
double g_st[8]; int g_fp_top; uint16_t g_fpu_cw;
ptrdiff_t g_mem_base;
uint32_t g_fs_base,g_gs_base;
uint16_t g_seg_cs,g_seg_ds,g_seg_es,g_seg_fs,g_seg_gs,g_seg_ss;
uint64_t g_mm[8]; xmm_t g_xmm[8]; uint32_t g_mxcsr;
void rc_run_native(u32);
void their_in(void) {
 g_eax=R.eax;g_ecx=R.ecx;g_edx=R.edx;g_ebx=R.ebx;g_esp=R.esp;g_ebp=R.ebp;g_esi=R.esi;g_edi=R.edi;
 memset(g_st,0,sizeof g_st); memset(g_st_i64,0,sizeof g_st_i64); g_fp_top=0;
 for(int i=0;i<8 && (R.tag>>((R.top+i)&7)&1);i++){g_st[i]=(double)ST(i);g_fp_top++;}
 g_fpu_cw=fnstcw();g_flag_k=g_flag_a=g_flag_b=g_flag_cf=0;
}
void their_out(void) {
 R.eax=g_eax;R.ecx=g_ecx;R.edx=g_edx;R.ebx=g_ebx;R.esp=g_esp;R.ebp=g_ebp;R.esi=g_esi;R.edi=g_edi;
 R.top=R.tag=0;
 for(int i=g_fp_top-1;i>=0 && i<8;i--) fpush((f80)g_st[i]);
}
static uint32_t target;
static void native_call(void) { their_out();rc_run_native(target);their_in();g_flag_k=FK_EFLAGS;g_flag_a=R.eflags;g_flag_b=0;g_flag_cf=R.eflags&1; }
recomp_func_t recomp_lookup_manual(uint32_t a) {target=a;return native_call;}

recomp_func_t recomp_lookup(uint32_t a){return 0;}
recomp_func_t recomp_lookup_import(uint32_t a){return 0;}
uint32_t g_icall_trace[ICALL_TRACE_SIZE],g_icall_from[ICALL_TRACE_SIZE];
uint32_t g_icall_trace_idx,g_icall_count;
