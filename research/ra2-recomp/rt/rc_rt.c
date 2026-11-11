/* rc_rt.c: runtime for lifted code: state, call dispatch, guest stack. */
#include <stdio.h>
#include <stdlib.h>
#include "rc.h"
RCState R;
struct rc_fn { u32 addr; void (*fn)(void); };
extern const struct rc_fn rc_table[];
extern const int rc_table_n;
u32 rc_target, rc_host_esp, rc_sw_before, rc_sw_after, rc_fin_n, rc_fout_n;
f80 rc_fin[8] __attribute__((aligned(16)));   /* 16-byte stride used by bridge.S */
f80 rc_fout[8] __attribute__((aligned(16)));
long rc_native_calls, rc_lifted_calls;
void rc_native_raw(u32 target) __asm__("rc_native");

static int off_init; static u32 *off_list; static int off_n;
static int is_off(u32 a)
{   /* RC_OFF=file with hex addresses: run these functions as original code (bisect aid) */
    if (!off_init) {
        off_init = 1;
        const char *p = getenv("RC_OFF");
        FILE *f = p ? fopen(p, "r") : 0;
        if (f) { u32 x; off_list = malloc(4 * 65536); while (off_n < 65536 && fscanf(f, "%x", &x) == 1) off_list[off_n++] = x; fclose(f); }
    }
    for (int i = 0; i < off_n; i++) if (off_list[i] == a) return 1;
    return 0;
}
void (*rc_lookup(u32 a))(void)
{
    if (off_n || !off_init) { if (is_off(a)) return 0; }
    int lo = 0, hi = rc_table_n - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (rc_table[m].addr == a) return rc_table[m].fn;
        if (rc_table[m].addr < a) lo = m + 1; else hi = m - 1;
    }
    return 0;
}
static void native_with_fpu(u32 target)
{
    /* move every valid guest x87 value to the real FPU, deepest first */
    int n = 0; f80 v[8];
    for (int i = 7; i >= 0; i--) if (R.tag >> ((R.top + i) & 7) & 1) v[n++] = ST(i);
    for (int i = 0; i < n; i++) rc_fin[i] = v[i];
    rc_fin_n = n; R.tag = 0;
    rc_native_calls++;
    /* "call" in the bridge writes a host return address into the slot that holds the
     * game return address. Put the game value back so stack memory matches the original. */
    u32 slot = R.esp, saved = M32(slot);
    rc_native_raw(target);
    M32(slot) = saved;
    for (int i = (int)rc_fout_n - 1; i >= 0; i--) fpush(rc_fout[i]);
}
static int trace_on = -1; static long trace_n;
void rc_call(u32 target)
{
    if (trace_on < 0) trace_on = getenv("RC_TRACE") != 0;
    if (trace_on && (trace_n++ < (getenv("RC_TRACE")[0] == 0x32 ? 2000000 : 300) || trace_n % 100000 == 0))
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
/* guest stack for lifted code: plain process memory, outside the game image */
static u32 guest_stack[1 << 18];
u32 rc_stack_top(void) { return (u32)(uintptr_t)&guest_stack[(1 << 18) - 64]; }
void rc_run_native(u32 target) { native_with_fpu(target); }
/* entry from original-style callers (difftest): see rc_entry_tramp in bridge.S */
u32 rc_entry_addr, rc_entry_esp;
static u32 private_stack[1 << 18] __attribute__((aligned(16)));
u32 rc_private_stack_top = (u32)(uintptr_t)&private_stack[(1 << 18) - 16];
void rc_entry_c(void)
{
    R.top = 0; R.tag = 0; R.df = 0;
    rc_call(rc_entry_addr);
    /* hand x87 results back: rc_fout[0] = ST(0); the tramp loads deepest first */
    int n = 0;
    while (n < 8 && (R.tag >> ((R.top + n) & 7) & 1)) { rc_fout[n] = ST(n); n++; }
    rc_fout_n = n;
}
