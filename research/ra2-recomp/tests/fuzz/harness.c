/* harness.c: run each instruction example as real bytes and as lifted C on random
 * states; compare registers, flags, memory, x87 stack, condition bits and status.
 * usage: harness [-n states] [-p 53|64|both] [-f key-substring] [-s seed] [-v] */
#define _GNU_SOURCE
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "fz.h"

RCState R; RCAux rc_aux;
void (**rc_fpv)(void); u32 rc_fp_lo, rc_fp_n; long rc_lifted_calls;
static u64 fz_rdtsc(void) { return 0x123456789abcull; }
static void fz_cpuid(u32 *a, u32 *b, u32 *c, u32 *d) { *a = *b = *c = *d = 0; }
u64 (*rc_rdtsc_hook)(void) = fz_rdtsc; void (*rc_cpuid_hook)(u32 *, u32 *, u32 *, u32 *) = fz_cpuid;
FZFlags FZF;
u32 FZI[8];
int FZ_exit;
u32 FZ_target, FZ_IND = 0x30000000u;
u32 FZ_fmask, FZ_rmask[8], FZ_cmask, FZ_xmask, FZ_mm_addr, FZ_mm_mask;

/* native side (native.S) */
u32 fz_nin[8], fz_nin_flags, fz_nout[8], fz_nout_flags, fz_host_esp, fz_code;
int fz_nout_exit;
u8 fz_nin_fpu[108], fz_nout_fpu[108];
extern u32 fz_after_ptr[3];
void fz_native(void);

static sigjmp_buf jb;
static volatile int in_run;
static void on_sig(int s, siginfo_t *si, void *uc) { (void)si; (void)uc; if (in_run) siglongjmp(jb, s); _exit(99); }

/* lifted side stubs */
void rc_call(u32 t) { if (!FZ_exit) FZ_exit = t == FZ_target ? 1 : t == FZ_IND ? 2 : 3; }
void rc_bad_jump(u32 f, u32 t) { (void)f; (void)t; siglongjmp(jb, 200); }
void rc_unreachable(u32 a) { (void)a; siglongjmp(jb, 201); }

/* ---- random ---- */
static u64 rs;
static u32 rnd32(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (u32)(rs >> 16) ^ (u32)rs; }
static u32 rnd(u32 n) { return rnd32() % n; }
static const u32 SPEC[] = {0, 1, 0xffffffffu, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x10000,
                           0x7fffffffu, 0x80000000u, 0x80000001u, 0xfffffffeu, 0xffffff80u, 0xffff8000u, 2, 31, 32, 33, 8, 16};
static u32 rnd_val(void)
{
    switch (rnd(4)) {
    case 0: return rnd32();
    case 1: return (u32)((int)rnd(33) - 16);
    case 2: return SPEC[rnd(sizeof SPEC / 4)];
    default: {
        u32 v = 0;
        for (int i = 0; i < 4; i++) { u32 b; switch (rnd(5)) { case 0: b = 0; break; case 1: b = 0xff; break; case 2: b = 0x80; break; case 3: b = 0x7f; break; default: b = rnd(256); } v |= b << (8 * i); }
        return v;
    }
    }
}
/* x87 values: always valid encodings (the game never makes unnormals) */
static void put80(u8 *p, u64 man, u32 se) { memcpy(p, &man, 8); p[8] = (u8)se; p[9] = (u8)(se >> 8); }
static void rnd_f80(u8 *p)
{
    u32 s = rnd(2) << 15;
    u64 m = ((u64)rnd32() << 32) | rnd32();
    switch (rnd(14)) {
    case 0: put80(p, 0, s); break;                                                /* +-0 */
    case 1: put80(p, m >> 1 >> rnd(60), s); break;                                /* denormal */
    case 2: put80(p, 1ull << 63, s | 0x7fff); break;                              /* inf */
    case 3: put80(p, (3ull << 62) | (m >> 2 >> rnd(62)), s | 0x7fff); break;      /* QNaN */
    case 4: put80(p, (1ull << 63) | (((m >> 2) | 1) >> rnd(61)), s | 0x7fff); break; /* SNaN */
    case 5: put80(p, 3ull << 62, 0xffff); break;                                  /* indefinite */
    case 6: { f80 v = (f80)(int)(rnd(4001) - 2000); if (rnd(3) == 0) v += 0.5L; memcpy(p, &v, 10); break; }
    case 7: { static const double B[] = {2147483647.0, 2147483648.0, -2147483648.0, -2147483649.0, 32767.0, 32768.0, -32768.0, -32769.0,
                                         9223372036854775807.0, -9223372036854775808.0, 4294967296.0, 65536.0, 0.5, 1.5, 2.5, -0.5};
              f80 v = (f80)B[rnd(16)]; if (rnd(2)) v += (f80)((int)rnd(5) - 2); if (rnd(3) == 0) v += 0.5L; memcpy(p, &v, 10); break; }
    case 8: put80(p, (1ull << 63) | m, s | (0x3fff - 70 + rnd(140))); break;      /* near 1 */
    case 9: put80(p, (1ull << 63) | m, s | (1 + rnd(0x7ffe))); break;             /* any normal */
    case 10: { double d; u64 b = ((u64)rnd32() << 32) | rnd32(); b = (b & 0x800fffffffffffffull) | ((u64)(1023 - 80 + rnd(160)) << 52); memcpy(&d, &b, 8); f80 v = d; memcpy(p, &v, 10); break; }
    case 11: { float f; u32 b = (rnd32() & 0x807fffffu) | ((127 - 40 + rnd(80)) << 23); memcpy(&f, &b, 4); f80 v = f; memcpy(p, &v, 10); break; }
    case 12: put80(p, (1ull << 63) | (m & ~0x7ffull) | (rnd(2) << 10), s | (0x3fff + rnd(70))); break;  /* ties at 53 bits */
    default: put80(p, (1ull << 63) | m, s | (rnd(2) ? 0x0001 + rnd(40) : 0x7ffe - rnd(40))); break; /* tiny / huge */
    }
}
static void rnd_mem(u8 *p, int fmt)
{
    switch (fmt) {
    case MF_F32: {
        u8 t[10]; rnd_f80(t); f80 v; memcpy(&v, t, 10); float f = (float)v; u32 b; memcpy(&b, &f, 4);
        if (rnd(6) == 0) b = (rnd32() & 0x807fffffu) | (rnd(2) ? 0x7f800000u : 0); /* nan/inf or denormal */
        if (rnd(10) == 0) b = 0x7f800001u | (rnd32() & 0x803fffffu);              /* SNaN */
        memcpy(p, &b, 4); break; }
    case MF_F64: {
        u8 t[10]; rnd_f80(t); f80 v; memcpy(&v, t, 10); double d = (double)v; u64 b; memcpy(&b, &d, 8);
        if (rnd(6) == 0) b = ((((u64)rnd32() << 32) | rnd32()) & 0x800fffffffffffffull) | (rnd(2) ? 0x7ff0000000000000ull : 0);
        if (rnd(10) == 0) b = 0x7ff0000000000001ull | ((((u64)rnd32() << 32) | rnd32()) & 0x8003ffffffffffffull);
        memcpy(p, &b, 8); break; }
    case MF_F80: rnd_f80(p); break;
    case MF_I16: { u16 v = (u16)rnd_val(); memcpy(p, &v, 2); break; }
    case MF_I32: { u32 v = rnd_val(); memcpy(p, &v, 4); break; }
    case MF_I64: { u32 v[2] = {rnd_val(), rnd(3) == 0 ? rnd_val() : (u32)-(s32)(rnd(2))}; memcpy(p, v, 8); break; }
    case MF_CW: { u16 v = (u16)((rnd32() & 0x1f00) | 0x7f); memcpy(p, &v, 2); break; }
    default: if (rnd(2)) { u32 v = rnd_val(); memcpy(p, &v, 4); } break;
    }
}

/* ---- state ---- */
typedef struct {
    u32 r[8]; u32 fl;          /* eflags */
    u16 cw, ex;                /* control word; exception bits 0..5 */
    u32 top, tag, c[4];        /* tag: valid mask by physical register */
    u8 st[8][10];              /* physical registers */
    int exit;
} St;

static u32 fulltag(const St *s)
{
    u32 tw = 0;
    for (int i = 0; i < 8; i++) {
        u32 t = 3;
        if (s->tag >> i & 1) {
            u64 m; memcpy(&m, s->st[i], 8); u32 e = (s->st[i][8] | s->st[i][9] << 8) & 0x7fff;
            t = (e == 0 && m == 0) ? 1 : (e == 0 || e == 0x7fff || !(m >> 63)) ? 2 : 0;
        }
        tw |= t << (2 * i);
    }
    return tw;
}
static void to_fsave(const St *s, u8 *img)
{
    memset(img, 0, 108);
    u32 sw = s->ex | s->c[0] << 8 | s->c[1] << 9 | s->c[2] << 10 | s->top << 11 | s->c[3] << 14;
    u32 w[7] = {0xffff0000u | s->cw, 0xffff0000u | sw, 0xffff0000u | fulltag(s), 0, 0, 0, 0};
    memcpy(img, w, 28);
    for (int i = 0; i < 8; i++) memcpy(img + 28 + 10 * i, s->st[(s->top + i) & 7], 10);
}
static void from_fsave(const u8 *img, St *s)
{
    u32 w[7]; memcpy(w, img, 28);
    u32 sw = w[1] & 0xffff, tw = w[2] & 0xffff;
    s->cw = (u16)w[0]; s->ex = sw & 0x7f;
    s->c[0] = sw >> 8 & 1; s->c[1] = sw >> 9 & 1; s->c[2] = sw >> 10 & 1; s->c[3] = sw >> 14 & 1;
    s->top = sw >> 11 & 7; s->tag = 0;
    for (int i = 0; i < 8; i++) if ((tw >> (2 * i) & 3) != 3) s->tag |= 1u << i;
    for (int i = 0; i < 8; i++) memcpy(s->st[(s->top + i) & 7], img + 28 + 10 * i, 10);
}

/* ---- memory ---- */
#define ARENA 0x20000000u
#define ARENA_SZ 0x10000u
#define PG 4096u
struct region { u32 lo, n; u8 *orig, *nat; };
static struct region reg[16]; static int nreg;
static u32 mapped[16]; static int nmapped;
static int map_page(u32 p)
{
    for (int i = 0; i < nmapped; i++) if (mapped[i] == p) return 1;
    void *r = mmap((void *)(uintptr_t)p, PG, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (r != (void *)(uintptr_t)p) { if (r != MAP_FAILED) munmap(r, PG); return 0; }
    mapped[nmapped++] = p; return 1;
}
static void add_region(u32 lo, u32 n)
{
    for (int i = 0; i < nreg; i++) if (reg[i].lo == lo) return;
    reg[nreg].lo = lo; reg[nreg].n = n; reg[nreg].orig = malloc(n); reg[nreg].nat = malloc(n); nreg++;
}
static void put_stub(u32 at, int k)
{
    u8 *p = (u8 *)(uintptr_t)at; u32 cell = (u32)(uintptr_t)&fz_after_ptr[k];
    p[0] = 0xff; p[1] = 0x25; memcpy(p + 2, &cell, 4);
}
static void fill(u32 lo, u32 n) { u8 *p = (u8 *)(uintptr_t)lo; for (u32 i = 0; i < n; i += 4) { u32 v = rnd32(); memcpy(p + i, &v, 4); } }

/* ---- runs ---- */
static void run_lifted(const struct fzex *x, const St *in, St *out)
{
    for (int i = 0; i < 8; i++) ((u32 *)&R)[i] = in->r[i];
    R.df = in->fl >> 10 & 1; R.top = in->top; R.tag = in->tag;
    for (int i = 0; i < 4; i++) (&R.c0)[i] = in->c[i];
    memset(R.st, 0, sizeof R.st);
    for (int i = 0; i < 8; i++) memcpy(&R.st[i], in->st[i], 10);
    FZF.cf = in->fl & 1; FZF.pf = in->fl >> 2 & 1; FZF.zf = in->fl >> 6 & 1; FZF.sf = in->fl >> 7 & 1; FZF.of = in->fl >> 11 & 1;
    FZ_exit = 0; FZ_target = x->target;
    u32 env[7] = {0xffff0000u | in->cw, 0xffff0000u | in->ex, 0xffffffffu, 0, 0, 0, 0};
    __asm__ volatile("fninit\n\tfldenv %0" :: "m"(env) : "memory");
    x->lift();
    u16 cw, sw;
    __asm__ volatile("fnstcw %0\n\tfnstsw %1\n\tfninit" : "=m"(cw), "=m"(sw) :: "memory");
    for (int i = 0; i < 8; i++) out->r[i] = ((u32 *)&R)[i];
    out->fl = FZF.cf | FZF.pf << 2 | FZF.zf << 6 | FZF.sf << 7 | R.df << 10 | FZF.of << 11;
    out->cw = cw; out->ex = sw & 0x7f; out->top = R.top & 7; out->tag = R.tag & 0xff;
    for (int i = 0; i < 4; i++) out->c[i] = (&R.c0)[i] & 1;
    for (int i = 0; i < 8; i++) memcpy(out->st[i], &R.st[i], 10);
    out->exit = FZ_exit;
}
static void run_native(const St *in, St *out)
{
    memcpy(fz_nin, in->r, 32); fz_nin_flags = in->fl | 0x202; to_fsave(in, fz_nin_fpu);
    fz_native();
    memcpy(out->r, fz_nout, 32); out->fl = fz_nout_flags; from_fsave(fz_nout_fpu, out); out->exit = fz_nout_exit;
    __asm__ volatile("fninit" ::: "memory");
}

static const char *RN[8] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
static void show(const char *tag, const St *s, int x87)
{
    printf("    %s:", tag);
    for (int i = 0; i < 8; i++) printf(" %s=%08x", RN[i], s->r[i]);
    printf(" fl=%03x exit=%d\n", s->fl & 0xcd5, s->exit);
    if (x87) {
        printf("      cw=%04x ex=%02x top=%u tag=%02x c=%u%u%u%u", s->cw, s->ex, s->top, s->tag, s->c[3], s->c[2], s->c[1], s->c[0]);
        for (int i = 0; i < 8; i++) if (s->tag >> ((s->top + i) & 7) & 1) {
            const u8 *p = s->st[(s->top + i) & 7]; printf(" st%d=%02x%02x:", i, p[9], p[8]);
            for (int k = 7; k >= 0; k--) printf("%02x", p[k]);
        }
        printf("\n");
    }
}

static int opt_n = 2000, opt_v;
static long total_mis, total_cmp;

/* returns number of mismatching states; -1 if the example cannot be set up */
static int run_example(const struct fzex *x, int pc, u64 seed, long *ncmp, long *nfault, const char **why)
{
    rs = seed * 0x9e3779b97f4a7c15ull + 12345 + x->addr;
    nreg = 0; nmapped = 0;
    add_region(ARENA, ARENA_SZ);
    u32 nxt = x->addr + x->len;
    /* code pages, branch target page, absolute data pages */
    int ok = 1;
    for (u32 p = x->addr & ~(PG - 1); p < nxt + 6; p += PG) ok &= map_page(p);
    if (x->kind & K_BR) for (u32 p = x->target & ~(PG - 1); p < x->target + 6; p += PG) ok &= map_page(p);
    int abs = x->mem && x->base < 0 && x->index < 0;
    if (abs) for (u32 p = x->disp & ~(PG - 1); p < x->disp + (u32)x->msize; p += PG) { ok &= map_page(p); add_region(p, PG); }
    if (!ok) { *why = "cannot map pages"; goto unmap; }
    int mis = 0; long tries = 0; *ncmp = 0; *nfault = 0;
    St in, nat, lif;
    static u8 *code_save;
    (void)code_save;
    while (*ncmp + *nfault < opt_n && tries < 6L * opt_n) {
        tries++;
        memset(&in, 0, sizeof in);
        for (int i = 0; i < 8; i++) in.r[i] = rnd_val();
        in.fl = (rnd32() & 0x8d5) | (rnd(4) == 0 ? 0x400 : 0);
        in.r[4] = ARENA + 0x8000 + 4 * (rnd(129) - 64);
        if ((x->kind & K_LEAVE) || rnd(2)) in.r[5] = in.r[4] + 4 * rnd(32);
        if (x->kind & K_STRING) {
            in.r[6] = ARENA + 0xa000 + rnd(1024) - 512; in.r[7] = ARENA + 0xc000 + rnd(1024) - 512;
            if (rnd(2)) { in.r[6] &= ~3u; in.r[7] &= ~3u; }
            if (x->kind & K_REP) in.r[1] = rnd(4) == 0 ? 0 : rnd(41);
        }
        if (x->kind & K_BTMEM) in.r[x->btreg] = (u32)((int)rnd(2048) - 1024);
        if ((x->kind & K_DIV) && rnd(4)) {
            int sg = x->kind & K_IDIV;
            if (x->dsize == 4) in.r[2] = sg ? (u32)((s32)in.r[0] >> 31) : 0;
            else if (x->dsize == 2) in.r[2] = (in.r[2] & 0xffff0000u) | (sg ? (u32)(u16)((s16)in.r[0] >> 15) : 0);
            else in.r[0] = (in.r[0] & 0xffff00ffu) | (sg ? (u32)(u8)((s8)in.r[0] >> 7) << 8 : 0);
        }
        /* fresh random memory near the places the instruction can touch */
        fill(ARENA + 0x1000, 0x2400); fill(ARENA + 0x7000, 0x1800); fill(ARENA + 0x9800, 0x1000); fill(ARENA + 0xb800, 0x1000);
        for (int i = 1; i < nreg; i++) fill(reg[i].lo, reg[i].n);
        /* memory operand: choose registers so that it points into the arena */
        u32 ea = 0;
        if (x->mem) {
            u32 T = ARENA + 0x2000 + rnd(4096) - 2048;
            if (rnd(4)) T &= ~(u32)((x->msize >= 4 ? 4 : x->msize) - 1);
            if (abs) T = x->disp;
            else if (x->index < 0) in.r[x->base] = T - x->disp;
            else if (x->base == x->index) { u32 k = 1 + x->scale; T -= (T - x->disp) % k; in.r[x->base] = (T - x->disp) / k; }
            else if (x->base < 0) { u32 k = x->scale; T -= (T - x->disp) % k; in.r[x->index] = (T - x->disp) / k; }
            else { s32 iv = (s32)rnd(129) - 64; in.r[x->index] = (u32)iv; in.r[x->base] = T - x->disp - (u32)iv * x->scale; }
            ea = T;
            if (x->mfmt != MF_INT || rnd(2)) rnd_mem((u8 *)(uintptr_t)ea, x->mfmt == MF_INT ? MF_I32 : x->mfmt);
        }
        if (x->kind & K_IND) { if (x->indreg >= 0) in.r[x->indreg] = FZ_IND; else M32(ea) = FZ_IND; }
        if (x->kind & K_RET) M32(in.r[4]) = FZ_IND;
        if (x->kind & K_POPF) M32(in.r[4]) = (rnd32() & 0xcd5) | 2;
        /* x87 */
        in.top = rnd(8); in.cw = 0x37f;
        if (x->kind & K_X87) {
            in.cw = (u16)((pc == 53 ? 0x27f : 0x37f) | rnd(4) << 10);
            int d = rnd(9);
            for (int i = 0; i < d; i++) { u32 ph = (in.top + i) & 7; in.tag |= 1u << ph; rnd_f80(in.st[ph]); }
            for (int i = 0; i < 4; i++) in.c[i] = rnd(2);
            if (rnd(4) == 0) in.ex = rnd(64);
        }
        /* code and stubs last (a data page can share a page with the code) */
        memcpy((void *)(uintptr_t)x->addr, x->bytes, x->len);
        put_stub(nxt, 0);
        if (x->kind & K_BR) put_stub(x->target, 1);
        put_stub(FZ_IND, 2);
        for (int i = 0; i < nreg; i++) memcpy(reg[i].orig, (void *)(uintptr_t)reg[i].lo, reg[i].n);
        /* masks */
        memcpy(FZI, in.r, 32);
        FZ_fmask = 0xcc5; for (int i = 0; i < 8; i++) FZ_rmask[i] = 0xffffffffu;
        FZ_cmask = (x->kind & K_X87) ? 0 : 0xf; FZ_xmask = 0x3f; FZ_mm_addr = 0; FZ_mm_mask = 0;
        x->mask();
        /* native */
        int sn, sl;
        fz_code = x->addr;
        in_run = 1;
        if (!(sn = sigsetjmp(jb, 1))) run_native(&in, &nat);
        in_run = 0;
        if (sn) { __asm__ volatile("cld\n\tfninit" ::: "memory"); }
        if (!sn && (x->kind & K_X87) && (nat.ex & 0x40)) {   /* x87 stack overflow/underflow: not a valid state */
            for (int i = 0; i < nreg; i++) memcpy((void *)(uintptr_t)reg[i].lo, reg[i].orig, reg[i].n);
            continue;
        }
        for (int i = 0; i < nreg; i++) { memcpy(reg[i].nat, (void *)(uintptr_t)reg[i].lo, reg[i].n); memcpy((void *)(uintptr_t)reg[i].lo, reg[i].orig, reg[i].n); }
        /* lifted */
        in_run = 1;
        if (!(sl = sigsetjmp(jb, 1))) run_lifted(x, &in, &lif);
        in_run = 0;
        if (sl) __asm__ volatile("cld\n\tfninit" ::: "memory");
        char diff[512]; int dn = 0; diff[0] = 0;
#define D(...) (dn += snprintf(diff + dn, sizeof diff - dn > 0 ? sizeof diff - dn : 0, __VA_ARGS__))
        if (sn || sl) {
            if (sn == sl) { (*nfault)++; continue; }
            D(" fault native=%d lifted=%d", sn, sl);
        } else {
            (*ncmp)++;
            for (int i = 0; i < 8; i++) if ((nat.r[i] ^ lif.r[i]) & FZ_rmask[i]) D(" %s", RN[i]);
            if ((nat.fl ^ lif.fl) & FZ_fmask) D(" flags(%03x)", (nat.fl ^ lif.fl) & FZ_fmask);
            if (nat.exit != lif.exit) D(" exit");
            for (int i = 0; i < nreg; i++) {
                u8 *a = reg[i].nat, *b = (u8 *)(uintptr_t)reg[i].lo;
                if (memcmp(a, b, reg[i].n)) {
                    for (u32 k = 0; k < reg[i].n; k++) if (a[k] != b[k]) {
                        u32 ad = reg[i].lo + k;
                        if (FZ_mm_mask && ad >= FZ_mm_addr && ad < FZ_mm_addr + 4 && !((a[k] ^ b[k]) & (FZ_mm_mask >> 8 * (ad - FZ_mm_addr)))) continue;
                        D(" mem@%08x(%02x/%02x)", ad, a[k], b[k]); break;
                    }
                }
            }
            if (x->kind & K_X87) {
                if (nat.top != lif.top) D(" top");
                if (nat.tag != lif.tag) D(" tag");
                for (int i = 0; i < 8; i++) if ((nat.tag & lif.tag) >> i & 1 && memcmp(nat.st[i], lif.st[i], 10)) { D(" st%d", (i - nat.top) & 7); }
                for (int i = 0; i < 4; i++) if (FZ_cmask >> i & 1 && nat.c[i] != lif.c[i]) D(" C%d", i);
                /* With a NaN operand the CPU reports only IE; the lifted code converts a
                 * denormal memory operand first and also sets DE. The game never reads DE. */
                if (nat.ex & 1) FZ_xmask &= ~2u;
                if ((nat.ex ^ lif.ex) & FZ_xmask) D(" ex(%02x/%02x)", nat.ex & 0x3f, lif.ex & 0x3f);
                if ((nat.cw ^ lif.cw) & 0x1f3f) D(" cw");
            } else {
                for (int i = 0; i < 4; i++) if (FZ_cmask >> i & 1 && in.c[i] != lif.c[i]) D(" C%d", i);
            }
        }
        if (dn) {
            if (mis < 3 || opt_v) {
                printf("  diff%s\n", diff);
                show("in    ", &in, x->kind & K_X87);
                if (!sn) show("native", &nat, x->kind & K_X87);
                if (!sl) show("lifted", &lif, x->kind & K_X87);
                if (x->mem) printf("    ea=%08x\n", ea);
            }
            mis++;
        }
        for (int i = 0; i < nreg; i++) memcpy((void *)(uintptr_t)reg[i].lo, reg[i].orig, reg[i].n);
    }
    if (*ncmp == 0) *why = "no valid state";
unmap:
    for (int i = 0; i < nmapped; i++) munmap((void *)(uintptr_t)mapped[i], PG);
    for (int i = 1; i < nreg; i++) { free(reg[i].orig); free(reg[i].nat); }
    nreg = 1;
    return ok ? mis : -1;
}

int main(int argc, char **argv)
{
    const char *filt = 0; int pcs = 3; u64 seed = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n")) opt_n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-p")) { i++; pcs = !strcmp(argv[i], "53") ? 1 : !strcmp(argv[i], "64") ? 2 : 3; }
        else if (!strcmp(argv[i], "-f")) filt = argv[++i];
        else if (!strcmp(argv[i], "-s")) seed = strtoull(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-v")) opt_v = 1;
    }
    setvbuf(stdout, 0, _IOLBF, 0);
    static u8 altstack[65536];
    stack_t ss = {.ss_sp = altstack, .ss_size = sizeof altstack};
    sigaltstack(&ss, 0);
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_sig; sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    int sigs[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGTRAP};
    for (int i = 0; i < 5; i++) sigaction(sigs[i], &sa, 0);
    if (mmap((void *)ARENA, ARENA_SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) != (void *)ARENA ||
        mmap((void *)FZ_IND, PG, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) != (void *)FZ_IND) {
        printf("cannot map arena\n"); return 2;
    }
    long nex = 0, nbad = 0, nruns = 0, nsetup = 0;
    for (int k = 0; k < fz_n; k++) {
        const struct fzex *x = &fz_table[k];
        if (filt && !strstr(x->key, filt) && !strstr(x->text, filt)) continue;
        nex++;
        int bad = 0;
        for (int pc = 53; pc <= 64; pc += 11) {
            if (!(x->kind & K_X87) && pc == 53) continue;
            if ((x->kind & K_X87) && !(pcs & (pc == 53 ? 1 : 2))) continue;
            long ncmp, nf; const char *why = 0;
            printf("%s | %08x %s | pc%d\n", x->key, x->addr, x->text, pc);
            int m = run_example(x, pc, seed, &ncmp, &nf, &why);
            nruns++;
            if (m < 0 || why) { printf("  SETUP %s\n", why ? why : "failed"); nsetup++; continue; }
            total_cmp += ncmp;
            printf("  %s compared %ld, same fault %ld, mismatches %d\n", m ? "MISMATCH" : "ok", ncmp, nf, m);
            if (m) { bad = 1; total_mis += m; }
        }
        nbad += bad;
    }
    printf("SUMMARY examples %ld, runs %ld, compared states %ld, examples with mismatches %ld, mismatching states %ld, setup problems %ld\n",
           nex, nruns, total_cmp, nbad, total_mis, nsetup);
    return total_mis || nsetup ? 1 : 0;
}
