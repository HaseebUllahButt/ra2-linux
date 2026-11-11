/* rc_rt.c: runtime for lifted code: state, call dispatch, guest stack.
 * v2: per-thread state (Windows), nested entries (native code calls lifted code while
 * lifted code waits in a native call), function on/off switch per table entry. */
#include <stdio.h>
#include <stdlib.h>
#include "rc.h"
RC_TLS RCState R;
#ifdef _WIN32
RC_TLS RCAux rc_aux;
#else
static u32 private_stack[1 << 20] __attribute__((aligned(16)));
RCAux rc_aux = { .host_top = (u32)(uintptr_t)&private_stack[(1 << 20) - 16] };
#endif
struct rc_fn { u32 addr; void (*fn)(void); };
extern const struct rc_fn rc_table[];
extern const int rc_table_n;
long rc_native_calls, rc_lifted_calls;
void rc_native_asm(RCAux *a);          /* bridge.S */
#ifdef _WIN32
#include <windows.h>
#endif

/* ---- function table, on/off ----
 * rc_fp: one pointer per .text byte address (J17): lookup is one array read.
 * NULL = not lifted, or switched off (run the original code). */
static u32 RC_TEXT_LO, RC_TEXT_HI;       /* lowest and highest lifted address + 1 */
static void (**rc_fp)(void);
void (**rc_fpv)(void); u32 rc_fp_lo, rc_fp_n;   /* same map, for RC_CALLD in lifted code */
int rc_find(u32 a)
{
    int lo = 0, hi = rc_table_n - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (rc_table[m].addr == a) return m;
        if (rc_table[m].addr < a) lo = m + 1; else hi = m - 1;
    }
    return -1;
}
static void fp_init(void)
{
    RC_TEXT_LO = 0xffffffffu; RC_TEXT_HI = 0;
    for (int i = 0; i < rc_table_n; i++) {
        if (rc_table[i].addr < RC_TEXT_LO) RC_TEXT_LO = rc_table[i].addr;
        if (rc_table[i].addr >= RC_TEXT_HI) RC_TEXT_HI = rc_table[i].addr + 1;
    }
    void (**t)(void) = calloc(RC_TEXT_HI - RC_TEXT_LO, sizeof *t);
    if (!t) { fprintf(stderr, "[rc] no memory for the function map\n"); abort(); }
    for (int i = 0; i < rc_table_n; i++)
        if (rc_table[i].addr - RC_TEXT_LO < RC_TEXT_HI - RC_TEXT_LO) t[rc_table[i].addr - RC_TEXT_LO] = rc_table[i].fn;
    rc_fp = t; rc_fpv = t; rc_fp_lo = RC_TEXT_LO; rc_fp_n = RC_TEXT_HI - RC_TEXT_LO;
}
void rc_set_off(int idx, int off)
{
    if (!rc_fp) fp_init();
    u32 a = rc_table[idx].addr;
    if (a - RC_TEXT_LO < RC_TEXT_HI - RC_TEXT_LO) rc_fp[a - RC_TEXT_LO] = off ? 0 : rc_table[idx].fn;
}
static int off_init;
static void load_off_env(void)
{   /* RC_OFF=file with hex addresses: run these functions as original code (bisect aid) */
    off_init = 1;
    const char *p = getenv("RC_OFF");
    FILE *f = p ? fopen(p, "r") : 0;
    if (f) { u32 x; while (fscanf(f, "%x", &x) == 1) { int i = rc_find(x); if (i >= 0) rc_set_off(i, 1); } fclose(f); }
}
void (*rc_lookup(u32 a))(void)
{
#ifndef _WIN32
    if (!off_init) load_off_env();      /* Windows: rc_win.c sets the on/off map */
#endif
    if (!rc_fp) fp_init();
    return a - RC_TEXT_LO < RC_TEXT_HI - RC_TEXT_LO ? rc_fp[a - RC_TEXT_LO] : 0;
}

/* ---- rdtsc / cpuid (replay can replace the hooks) ---- */
static u64 real_rdtsc(void) { u32 lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return (u64)hi << 32 | lo; }
static void real_cpuid(u32 *a, u32 *b, u32 *c, u32 *d)
{ __asm__ volatile("cpuid" : "+a"(*a), "=b"(*b), "+c"(*c), "=d"(*d)); }
u64 (*rc_rdtsc_hook)(void) = real_rdtsc;
void (*rc_cpuid_hook)(u32 *a, u32 *b, u32 *c, u32 *d) = real_cpuid;

/* ---- per-thread set-up ---- */
#define HOST_STACK (8u << 20)
void rc_thread_init(void)
{
    if (rc_aux.host_top) return;
    u8 *s;
#ifdef _WIN32
    s = VirtualAlloc(0, HOST_STACK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    s = malloc(HOST_STACK);
#endif
    if (!s) { fprintf(stderr, "[rc] no memory for host stack\n"); abort(); }
    rc_aux.host_top = ((u32)(uintptr_t)(s + HOST_STACK) - 64) & ~15u;
    rc_aux.host_esp = 0;
    R.top = 0; R.tag = 0; R.df = 0;
}

/* ---- native calls ---- */
static void native_with_fpu(u32 target)
{
    RCAux *a = &rc_aux;
    /* move every valid guest x87 value to the real FPU, deepest first */
    int n = 0; f80 v[8];
    for (int i = 7; i >= 0; i--) if (R.tag >> ((R.top + i) & 7) & 1) v[n++] = ST(i);
    for (int i = 0; i < n; i++) a->fin[i].v = v[i];
    a->fin_n = n; R.tag = 0;
    rc_native_calls++;
    /* The bridge writes a host return address into the slot that holds the game
     * return address. Put the game value back so stack memory matches the original. */
    u32 slot = R.esp, saved = M32(slot);
    a->save[0] = R.eax; a->save[1] = R.ecx; a->save[2] = R.edx; a->save[3] = R.ebx;
    a->save[4] = R.esp; a->save[5] = R.ebp; a->save[6] = R.esi; a->save[7] = R.edi;
    a->target = target;
    rc_native_asm(a);
    /* a nested entry may have run on this thread; rc_aux is still this thread's */
    R.eax = a->save[0]; R.ecx = a->save[1]; R.edx = a->save[2]; R.ebx = a->save[3];
    R.esp = a->save[4]; R.ebp = a->save[5]; R.esi = a->save[6]; R.edi = a->save[7];
    R.eflags = a->eflags;
    M32(slot) = saved;
    for (int i = (int)a->fout_n - 1; i >= 0; i--) fpush(a->fout[i].v);
}
static int trace_on = -1; static long trace_n;
void rc_call(u32 target)
{
    if (__builtin_expect(trace_on, 0) && trace_on < 0) trace_on = getenv("RC_TRACE") != 0;
    if (__builtin_expect(trace_on, 0) && (trace_n++ < (getenv("RC_TRACE")[0] == 0x32 ? 2000000 : 300) || trace_n % 100000 == 0))
        fprintf(stderr, "[rc] call %08x from %08x esp=%08x ecx=%08x n=%ld %s\n", target, M32(R.esp), R.esp, R.ecx, trace_n, rc_lookup(target) ? "lifted" : "native");
    void (*f)(void) = rc_lookup(target);
    if (f) { rc_lifted_calls++; f(); }
    else native_with_fpu(target);
}
void rc_bad_jump(u32 from, u32 target)
{
    fprintf(stderr, "[rc] unknown jump target %08x at %08x\n", target, from);
    abort();
}
void rc_unreachable(u32 at)
{
    fprintf(stderr, "[rc] reached code that was not lifted at %08x\n", at);
    abort();
}
/* guest stack for lifted code in tests: plain process memory, outside the game image */
static u32 guest_stack[1 << 18];
u32 rc_stack_top(void) { return (u32)(uintptr_t)&guest_stack[(1 << 18) - 64]; }
void rc_run_native(u32 target) { rc_thread_init(); native_with_fpu(target); }

/* ---- entry from native code (entry thunks, rc_entry_tramp) ----
 * The asm saved the native registers in rc_aux.save and switched to the host stack.
 * Run the lifted function with R = those registers. Nested entries keep the outer
 * R on this host stack and put it back, so the outer lifted code sees no change. */
u32 rc_entry_addr;                      /* Linux test entry (rc_entry_tramp) */
RCAux *rc_entry_c(RCAux *a)
{
    u32 entry = a->entry ? a->entry : rc_entry_addr;
    a->entry = 0;
    RCState outer = R;                  /* only needed when nested; cheap enough */
    R.eax = a->save[0]; R.ecx = a->save[1]; R.edx = a->save[2]; R.ebx = a->save[3];
    R.esp = a->save[4]; R.ebp = a->save[5]; R.esi = a->save[6]; R.edi = a->save[7];
    a->ret = 0;
    u32 ret = M32(R.esp);
    R.top = 0; R.tag = 0; R.df = 0;
    a->depth++;
    void (*f)(void) = rc_lookup(entry);
    if (f) { rc_lifted_calls++; f(); }
    else { fprintf(stderr, "[rc] entry %08x is not lifted\n", entry); abort(); }
    a->depth--;
    /* hand x87 results back: fout[0] = ST(0); the asm loads deepest first */
    int n = 0;
    while (n < 8 && (R.tag >> ((R.top + n) & 7) & 1)) { a->fout[n].v = ST(n); n++; }
    a->fout_n = n;
    a->save[0] = R.eax; a->save[1] = R.ecx; a->save[2] = R.edx; a->save[3] = R.ebx;
    a->save[4] = R.esp; a->save[5] = R.ebp; a->save[6] = R.esi; a->save[7] = R.edi;
    a->ret = ret;
    a->eflags = (R.eflags & 0x8d5u) | 0x202u;   /* flags for FLAGRET callers; DF = 0 */
    R = outer;
    return a;
}
