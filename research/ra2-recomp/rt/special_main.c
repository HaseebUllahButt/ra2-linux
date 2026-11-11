/* special_main.c: typed differential tests for x87 math, jump tables and virtual calls.
 * Both sides start from the same register state R and the same guest stack.
 * Original: rc_native(addr) runs the game's machine code. Lifted: the C function.
 * Compared per input: EAX, EDX, the full x87 stack (80-bit bit patterns), the
 * scratch memory and a hash of the game's .data/.bss. Each test runs under both
 * x87 precision settings (64-bit: Linux default 0x37f, 53-bit: Windows 0x27f). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
#include "rc.h"
int pilot_map_image(const char *, int);
int pilot_hybrid_init(const char *);
void (*rc_lookup(u32 a))(void);
void rc_run_native(u32 target);
u32 rc_stack_top(void);

static unsigned char *scratch;      /* 64 KiB test memory */
#define SCR ((u32)(uintptr_t)scratch)

struct res { u32 eax, edx, depth; unsigned char st[8][10]; uint64_t h; };
static uint64_t fnv(const unsigned char *p, size_t n, uint64_t h) { for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ULL; } return h; }

struct input { u32 ecx, edx, eax; int nst; u32 stk[6]; int nfin; f80 fin[2]; };
static void setup(const struct input *in)
{
    u32 sp = rc_stack_top() & ~15u;
    for (int i = in->nst - 1; i >= 0; i--) { sp -= 4; M32(sp) = in->stk[i]; }
    sp -= 4; M32(sp) = 0xc0dec0deu;
    memset(&R, 0, sizeof R);
    R.esp = sp; R.ecx = in->ecx; R.edx = in->edx; R.eax = in->eax; R.ebx = 0x1111; R.esi = 0x2222; R.edi = 0x3333; R.ebp = 0x4444;
    for (int i = in->nfin - 1; i >= 0; i--) fpush(in->fin[i]);
    __asm__ volatile("fnclex" ::: "memory");   /* same sticky flags for both runs */
}
static void collect(struct res *r)
{
    memset(r, 0, sizeof *r);
    r->eax = R.eax; r->edx = R.edx;
    for (int i = 0; i < 8 && (R.tag >> ((R.top + i) & 7) & 1); i++) { memcpy(r->st[i], &ST(i), 10); r->depth++; }
    extern unsigned char *save_data;
    int dchg = memcmp((void *)0x7c9000, save_data, 0xb2d000 - 0x7c9000) != 0;
    r->h = fnv(scratch, 0x10000, dchg ? fnv((const unsigned char *)0x7c9000, 0xb2d000 - 0x7c9000, 0xcbf29ce484222325ULL) : 1);
}
unsigned char *save_data, *save_scr;
static void snap(void) { if (memcmp((void *)0x7c9000, save_data, 0xb2d000 - 0x7c9000)) memcpy(save_data, (void *)0x7c9000, 0xb2d000 - 0x7c9000); memcpy(save_scr, scratch, 0x10000); }
static void restore(void) { if (memcmp((void *)0x7c9000, save_data, 0xb2d000 - 0x7c9000)) memcpy((void *)0x7c9000, save_data, 0xb2d000 - 0x7c9000); memcpy(scratch, save_scr, 0x10000); }
static int snapped;

typedef void (*prep_fn)(int k, struct input *in);
static int run_case(u32 addr, prep_fn prep, int k, int quiet)
{
    struct input in; memset(&in, 0, sizeof in);
    prep(k, &in);
    snap();
    struct res a, b;
    setup(&in); rc_run_native(addr); collect(&a);
    restore();
    void (*f)(void) = rc_lookup(addr);
    setup(&in); f(); collect(&b);
    restore();
    int same = a.eax == b.eax && a.edx == b.edx && a.depth == b.depth && !memcmp(a.st, b.st, sizeof a.st) && a.h == b.h;
    if (!same && !quiet) {
        printf("  %08x case %d: eax %08x/%08x edx %08x/%08x depth %u/%u st0 %s mem %s\n", addr, k, a.eax, b.eax, a.edx, b.edx,
               a.depth, b.depth, memcmp(a.st, b.st, 10) ? "differs" : "same", a.h == b.h ? "same" : "differs");
    }
    return same;
}

/* ---- inputs ---- */
static double rnd_d(int k)
{
    static const double special[] = {0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 1e-310, 3.0e38, 1e300, 1.0/0.0, -1.0/0.0, 0.1, 255.5, 65536.0, 1e-7, 123456.789};
    if (k < 16) return special[k];
    srand(k * 7919);
    double m = (double)rand() / RAND_MAX, e = (rand() % 80) - 40;
    double v = m * pow(2.0, e);
    return rand() & 1 ? -v : v;
}
static float rnd_f(int k) { return (float)rnd_d(k); }
static void p_sqrt(int k, struct input *in) { double d = fabs(rnd_d(k)); if (k == 3) d = -1.0; memcpy(in->stk, &d, 8); in->nst = 2; }
static void p_exp(int k, struct input *in) { in->nfin = 1; in->fin[0] = (f80)(rnd_d(k + 1000) / 1e6) + (k % 64) - 32; }
static void p_atan(int k, struct input *in) { float x = rnd_f(k), y = rnd_f(k + 5000); if (k % 7 == 0) y = 0; memcpy(&in->stk[0], &x, 4); memcpy(&in->stk[1], &y, 4); in->nst = 2; }
static void p_ilen(int k, struct input *in) { srand(k); s32 *v = (s32 *)scratch; for (int i = 0; i < 3; i++) v[i] = (rand() % 200001) - 100000; in->ecx = SCR; }
static void p_mat(int k, struct input *in)
{
    float *m = (float *)scratch, *v = (float *)(scratch + 0x100);
    for (int i = 0; i < 12; i++) m[i] = rnd_f(k * 13 + i);
    for (int i = 0; i < 3; i++) v[i] = rnd_f(k * 17 + i + 400);
    memset(scratch + 0x200, 0, 16);
    in->ecx = SCR; in->stk[0] = SCR + 0x200; in->stk[1] = SCR + 0x100; in->nst = 2;
}
static void p_sw1(int k, struct input *in) { in->ecx = (u32)(k - 20); in->eax = 0x5555; }
static void p_sw2(int k, struct input *in) { *(u32 *)(scratch + 0x2c) = (u32)(k - 5); in->ecx = SCR; }
static void p_sw3(int k, struct input *in)
{
    static const u32 arrays[] = {0xa35d4c, 0xa35cdc, 0xa40354, 0xa3d27c};
    for (int i = 0; i < 4; i++) *(u32 *)arrays[i] = SCR + 0x1000 + i * 0x100;
    for (int i = 0; i < 0x100; i++) ((u32 *)(scratch + 0x1000))[i] = 0xab000000u + i;
    in->ecx = (u32)(k - 3); in->edx = (u32)(k % 5);
}
/* virtual call: the object's table slot 0x2c points to a host function or to game code */
static u32 vret;
static u32 __attribute__((thiscall)) host_kind(void *self) { return vret; }
static void p_vc1(int k, struct input *in)
{
    u32 *obj = (u32 *)scratch, *vt = (u32 *)(scratch + 0x400);
    obj[0] = (u32)(uintptr_t)vt;
    for (int i = 0; i < 32; i++) vt[i] = 0x401000;
    vt[0x2c / 4] = (u32)(uintptr_t)host_kind;
    vret = (u32)(k - 2);
    in->ecx = k == 0 ? 0 : SCR;
}
static void p_vc2(int k, struct input *in)
{
    /* slot 0x2c = game function 006e6cf0 (also lifted): reads this+0x2c, switch -> al */
    u32 *obj = (u32 *)scratch, *vt = (u32 *)(scratch + 0x400);
    obj[0] = (u32)(uintptr_t)vt;
    vt[0x2c / 4] = 0x6e6cf0;
    obj[0x2c / 4] = (u32)(k - 3);
    in->ecx = SCR;
}

static void p_strchr(int k, struct input *in)
{
    srand(k); int off = k & 7, len = rand() % 40;
    char *str = (char *)scratch + 0x800 + off;
    for (int i = 0; i < len; i++) str[i] = "abcdefgh\x80\xff"[rand() % 10];
    str[len] = 0;
    in->stk[0] = (u32)(uintptr_t)str; in->stk[1] = (u32)"abcx\x80\xff\0"[k % 7] & 0xff | (k & 0x100 ? 0x1200 : 0); in->nst = 2;
}
struct test { u32 addr; const char *what; prep_fn prep; int n; };
static const struct test tests[] = {
    {0x4ba410, "x87 fast square root (table)", p_sqrt, 5000},
    {0x787620, "x87 exp helper (frndint f2xm1 fscale)", p_exp, 5000},
    {0x4baba0, "x87 angle from two floats (+ftol, control word)", p_atan, 5000},
    {0x41b780, "x87 integer vector length (+sqrt +ftol tail jump)", p_ilen, 5000},
    {0x590cd0, "x87 3x4 matrix times vector", p_mat, 5000},
    {0x6b2df0, "byte+dword jump table on ECX", p_sw1, 140},
    {0x6e6cf0, "byte+dword jump table on this+0x2c", p_sw2, 70},
    {0x482c00, "jump table selecting global arrays", p_sw3, 50},
    {0x40dbd0, "virtual call to host code + jump table", p_vc1, 24},
    {0x40dc20, "virtual call to lifted game code + jump table", p_vc2, 64},
    {0x784130, "C library strchr (word-at-a-time scan)", p_strchr, 4000},
    {0, 0, 0, 0}
};

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    if (pilot_map_image(argv[1], 1) || pilot_hybrid_init(argv[1])) return 2;
    scratch = aligned_alloc(4096, 0x10000); memset(scratch, 0, 0x10000);
    save_data = malloc(0xb2d000 - 0x7c9000); save_scr = malloc(0x10000);
    int total = 0, exact = 0;
    for (const struct test *t = tests; t->addr; t++) {
        if (!rc_lookup(t->addr)) { printf("%08x NOT LIFTED %s\n", t->addr, t->what); total++; continue; }
        int ok = 1;
        for (int cw = 0; cw < 2; cw++) {
            pid_t p = fork();
            if (p == 0) {
                fldcw(cw ? 0x27f : 0x37f);
                int bad = 0;
                for (int k = 0; k < t->n; k++) if (!run_case(t->addr, t->prep, k, bad >= 3)) bad++;
                printf("%08x %-50s cw=%s: %d inputs, %d differ\n", t->addr, t->what, cw ? "53-bit" : "64-bit", t->n, bad);
                _exit(bad != 0);
            }
            int st; waitpid(p, &st, 0);
            if (!WIFEXITED(st) || WEXITSTATUS(st)) { ok = 0; if (!WIFEXITED(st)) printf("%08x crashed (signal %d)\n", t->addr, WTERMSIG(st)); }
        }
        total++; exact += ok;
        printf("%08x %s\n", t->addr, ok ? "EXACT" : "WRONG");
    }
    printf("special: %d functions, %d exact\n", total, exact);
    return exact != total;
}
