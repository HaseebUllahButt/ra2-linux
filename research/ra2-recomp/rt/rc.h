/* rc.h: state and helpers for mechanically lifted game.exe code.
 * Each lifted function is "void f_XXXXXXXX(void)". It keeps x86 registers in C locals
 * and syncs them with the global state R at calls and returns. Memory is the real
 * process memory (game image mapped at 0x400000, 32-bit process). */
#ifndef RC_H
#define RC_H
#include <stdint.h>
#include <string.h>
typedef uint32_t u32; typedef uint16_t u16; typedef uint8_t u8;
typedef int32_t s32; typedef int16_t s16; typedef int8_t s8;
typedef uint64_t u64; typedef int64_t s64;
typedef long double f80;

typedef struct {
    u32 eax, ecx, edx, ebx, esp, ebp, esi, edi;   /* offsets 0..28, used by bridge.S */
    u32 top, tag;                                 /* x87 stack top and valid mask   */
    u32 c0, c1, c2, c3;                           /* x87 condition bits             */
    u32 df;
    f80 st[8];
} RCState;
/* Per-thread state. R is the lifted register state of the current thread.
 * rc_aux holds the bridge and entry state (offsets used by bridge.S).
 * Windows build: both are thread-local. Linux test build: single thread, plain globals. */
#ifdef _WIN32
#define RC_TLS __thread
#else
#define RC_TLS
#endif
typedef union { f80 v; u8 b[16]; } f80s;              /* 16-byte stride for asm */
typedef struct RCAux {
    u32 save[8];         /*  0 eax ecx edx ebx esp ebp esi edi: registers at the asm edge */
    u32 host_esp;        /* 32 host esp inside rc_native (0 = not in a native call) */
    u32 host_top;        /* 36 top of this thread's private host stack */
    u32 target;          /* 40 */
    u32 sw_before;       /* 44 */
    u32 fin_n, fout_n;   /* 48, 52 */
    u32 entry;           /* 56 entry address set by an entry thunk */
    u32 ret;             /* 60 return address for the entry exit */
    u32 depth;           /* 64 lifted entry nesting depth */
    u32 pad[3];          /* 68 */
    f80s fin[8];         /* 80 */
    f80s fout[8];        /* 208 */
} RCAux;
extern RC_TLS RCState R;
extern RC_TLS RCAux rc_aux;
void rc_thread_init(void);           /* set up R/rc_aux for the current thread */

#define M8(a)  (*(u8  *)(uintptr_t)(u32)(a))
#define M16(a) (*(u16 *)(uintptr_t)(u32)(a))
#define M32(a) (*(u32 *)(uintptr_t)(u32)(a))
#define MF32(a) (*(float  *)(uintptr_t)(u32)(a))
#define MF64(a) (*(double *)(uintptr_t)(u32)(a))
#define PAR(r) ((u8)!__builtin_parity((r) & 0xff))

#define LOADREGS u32 eax=R.eax,ecx=R.ecx,edx=R.edx,ebx=R.ebx,esp=R.esp,ebp=R.ebp,esi=R.esi,edi=R.edi; \
    u8 cf=0,zf=0,sf=0,of=0,pf=0; (void)cf;(void)zf;(void)sf;(void)of;(void)pf
#define SAVE R.eax=eax;R.ecx=ecx;R.edx=edx;R.ebx=ebx;R.esp=esp;R.ebp=ebp;R.esi=esi;R.edi=edi
#define LOAD eax=R.eax;ecx=R.ecx;edx=R.edx;ebx=R.ebx;esp=R.esp;ebp=R.ebp;esi=R.esi;edi=R.edi

/* fs: segment (Windows thread block) */
static inline u32 FS32(u32 o) { u32 v; __asm__ volatile("movl %%fs:(%1), %0" : "=r"(v) : "r"(o) : "memory"); return v; }
static inline void FSW32(u32 o, u32 v) { __asm__ volatile("movl %1, %%fs:(%0)" :: "r"(o), "r"(v) : "memory"); }
static inline u32 FSBASE(void) { return FS32(0x18); }

/* calls */
void rc_call(u32 target);            /* any target: lifted or native */

void rc_bad_jump(u32 from, u32 target) __attribute__((noreturn));
void rc_unreachable(u32 at) __attribute__((noreturn));

/* x87 */
#define ST(i) R.st[(R.top + (i)) & 7]
static inline void fpush(f80 v) { R.top = (R.top - 1) & 7; R.st[R.top] = v; R.tag |= 1u << R.top; }
static inline void fpop(void) { R.tag &= ~(1u << R.top); R.top = (R.top + 1) & 7; }
static inline void fcmp(f80 a, f80 b) {
    R.c1 = 0;
    if (a > b) R.c0 = R.c2 = R.c3 = 0;
    else if (a < b) { R.c0 = 1; R.c2 = R.c3 = 0; }
    else if (a == b) { R.c3 = 1; R.c0 = R.c2 = 0; }
    else R.c0 = R.c2 = R.c3 = 1;
}
/* Status word: condition bits and TOP from R; exception flags from the real FPU,
 * because lifted x87 arithmetic runs on the real FPU and sets the same sticky flags. */
static inline u16 fsw(void) {
    u16 sw; __asm__ volatile("fnstsw %0" : "=a"(sw) :: "memory");
    return (u16)((sw & 0x80bf) | R.c3 << 14 | R.top << 11 | R.c2 << 10 | R.c1 << 9 | R.c0 << 8);
}
static inline void fldcw(u16 cw) { __asm__ volatile("fldcw %0" :: "m"(cw) : "memory"); }
static inline u16 fnstcw(void) { u16 cw; __asm__ volatile("fnstcw %0" : "=m"(cw) :: "memory"); return cw; }
static inline s16 fist16(f80 v) { s16 i; __asm__ volatile("fists %0" : "=m"(i) : "t"(v)); return i; }
static inline s32 fist32(f80 v) { s32 i; __asm__ volatile("fistl %0" : "=m"(i) : "t"(v)); return i; }
static inline s64 fist64(f80 v) { s64 i; __asm__ volatile("fistpll %0" : "=m"(i) : "t"(v) : "st"); return i; }
static inline f80 x_frndint(f80 v) { __asm__ volatile("frndint" : "+t"(v)); return v; }
static inline f80 x_fsqrt(f80 v) { __asm__ volatile("fsqrt" : "+t"(v)); return v; }
static inline f80 x_fsin(f80 v) { __asm__ volatile("fsin" : "+t"(v)); return v; }
static inline f80 x_fcos(f80 v) { __asm__ volatile("fcos" : "+t"(v)); return v; }
static inline f80 x_f2xm1(f80 v) { __asm__ volatile("f2xm1" : "+t"(v)); return v; }
static inline f80 x_fptan(f80 v) { __asm__ volatile("fptan\n\tfstp %%st(0)" : "+t"(v)); return v; }
static inline f80 x_fpatan(f80 y, f80 x) { f80 r; __asm__ volatile("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
static inline f80 x_fyl2x(f80 y, f80 x) { f80 r; __asm__ volatile("fyl2x" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
static inline f80 x_fyl2xp1(f80 y, f80 x) { f80 r; __asm__ volatile("fyl2xp1" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
static inline f80 x_fscale(f80 a, f80 b) { __asm__ volatile("fscale" : "+t"(a) : "u"(b)); return a; }
static inline f80 x_fprem(f80 a, f80 b, int one) {
    u16 sw;
    if (one) __asm__ volatile("fprem1\n\tfnstsw %1" : "+t"(a), "=a"(sw) : "u"(b));
    else __asm__ volatile("fprem\n\tfnstsw %1" : "+t"(a), "=a"(sw) : "u"(b));
    R.c0 = sw >> 8 & 1; R.c1 = sw >> 9 & 1; R.c2 = sw >> 10 & 1; R.c3 = sw >> 14 & 1;
    return a;
}
static inline void fxam(void) {
    u16 sw; f80 v = ST(0);
    if (!(R.tag >> R.top & 1)) { R.c3 = 1; R.c2 = 0; R.c0 = 1; R.c1 = 0; return; }
    __asm__ volatile("fxam\n\tfnstsw %0" : "=a"(sw) : "t"(v));
    R.c0 = sw >> 8 & 1; R.c1 = sw >> 9 & 1; R.c2 = sw >> 10 & 1; R.c3 = sw >> 14 & 1;
}
static inline f80 LD80(u32 a) { f80 v = 0; memcpy(&v, (void *)(uintptr_t)a, 10); return v; }
static inline void ST80(u32 a, f80 v) { memcpy((void *)(uintptr_t)a, &v, 10); }
#endif
