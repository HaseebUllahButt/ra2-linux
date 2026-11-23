/* MMX helpers in rc.h against the real MMX unit: 200,000 random pairs per form. */
#include <stdio.h>
#include <stdlib.h>
#include "rc.h"
RCState R; RCAux rc_aux;
u64 (*rc_rdtsc_hook)(void); void (*rc_cpuid_hook)(u32 *, u32 *, u32 *, u32 *);
static u64 rnd(void) { u64 r = 0; for (int i = 0; i < 4; i++) r = r << 16 | (rand() & 0xffff);
    switch (rand() % 6) { case 0: return r & 0x00ff00ff00ff00ffull; case 1: return r | 0x8000800080008000ull; case 2: return r & 0x7fff7fff7fff7fffull; default: return r; } }
#define HW(op) static u64 hw_##op(u64 a, u64 b) { __asm__ volatile("movq %0, %%mm0\n\tmovq %1, %%mm1\n\t" #op " %%mm1, %%mm0\n\tmovq %%mm0, %0\n\temms" : "+m"(a) : "m"(b) : "mm0", "mm1"); return a; }
HW(packssdw) HW(packuswb) HW(paddusb) HW(pmulhw) HW(pmullw) HW(punpckhwd) HW(punpcklbw) HW(pxor)
static u64 hw_psrlw(u64 a, u64 n) { __asm__ volatile("movq %0, %%mm0\n\tmovq %1, %%mm1\n\tpsrlw %%mm1, %%mm0\n\tmovq %%mm0, %0\n\temms" : "+m"(a) : "m"(n) : "mm0", "mm1"); return a; }
static u64 hw_psrad(u64 a, u64 n) { __asm__ volatile("movq %0, %%mm0\n\tmovq %1, %%mm1\n\tpsrad %%mm1, %%mm0\n\tmovq %%mm0, %0\n\temms" : "+m"(a) : "m"(n) : "mm0", "mm1"); return a; }
static u64 sw_pxor(u64 a, u64 b) { return a ^ b; }
static u64 sw_punpcklbw(u64 a, u64 b) { return mmx_punpcklbw(a, b & 0xffffffffu); }
int main(void)
{
    struct { const char *n; u64 (*h)(u64, u64), (*s)(u64, u64); int shift; } t[] = {
        {"packssdw", hw_packssdw, mmx_packssdw}, {"packuswb", hw_packuswb, mmx_packuswb}, {"paddusb", hw_paddusb, mmx_paddusb},
        {"pmulhw", hw_pmulhw, mmx_pmulhw}, {"pmullw", hw_pmullw, mmx_pmullw}, {"punpckhwd", hw_punpckhwd, mmx_punpckhwd},
        {"punpcklbw", hw_punpcklbw, sw_punpcklbw}, {"pxor", hw_pxor, sw_pxor}, {"psrlw", hw_psrlw, mmx_psrlw, 1}, {"psrad", hw_psrad, mmx_psrad, 1} };
    int bad = 0;
    for (unsigned k = 0; k < sizeof t / sizeof *t; k++) {
        int m = 0;
        for (int i = 0; i < 200000; i++) {
            u64 a = rnd(), b = t[k].shift ? (u64)(rand() % 40) : rnd();
            if (t[k].h(a, b) != t[k].s(a, b)) { if (m++ < 3) printf("%s %016llx %016llx: hw %016llx sw %016llx\n", t[k].n, a, b, t[k].h(a, b), t[k].s(a, b)); }
        }
        printf("%-10s %s (%d mismatches)\n", t[k].n, m ? "FAIL" : "OK", m); bad += m;
    }
    return bad != 0;
}
