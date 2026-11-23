/* difftest_rc.c: copy of ra2-1to1/runtime/difftest.c (commit f763e3c). Only run_child
 * changed: lifted code enters through rc_entry_tramp.
 * difftest.c: fork-based differential test of every translated function.
 * For each function and each input set, two child processes start from the SAME
 * memory state (fork copies the mapped game image, the heap and the House object).
 * Child 1 runs the ORIGINAL machine code, child 2 runs the translated C.
 * Each child reports: how it ended (return / fault / import trap / timeout), the return
 * value or fault data address, and a hash of all game globals (.data/.bss) and of the
 * test objects. A match requires normal returns, equal values and state, and no stubs. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <fcntl.h>

typedef unsigned int uint;
struct diff_entry { uint addr; void *fn; int nparams; int nreg; int fret; int rsize; };
extern const struct diff_entry diff_table[];
uint64_t pilot_call_orig(uint addr, uint ecx, uint edx, int n, const uint *args);
long double pilot_call_orig_f(uint addr, uint ecx, uint edx, int n, const uint *args);
extern int pilot_quiet;
extern int pilot_stub_calls;
extern void pilot_batch_prepare(uint, int, uint, uint, uint *) __attribute__((weak));

struct outcome { int kind; uint value; uint64_t hash; int stubs; unsigned char fp[10]; };
_Static_assert(sizeof(long double) >= 10, "x87 long double required");
static unsigned char return_fp[10];
enum { K_RET = 1, K_FAULT, K_TIMEOUT, K_ABORT };
static int out_fd;
static unsigned char *g_objs; static size_t g_objs_len;

static uint64_t fnv(const unsigned char *p, size_t n, uint64_t h)
{
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
    return h;
}
extern uint64_t pilot_fixture_api_hash;
static uint64_t state_hash(void)
{
    uint64_t h = fnv((const unsigned char *)0x7c9000, 0xb2d000 - 0x7c9000, 0xcbf29ce484222325ULL);
    h = fnv(g_objs, g_objs_len, h);
    return fnv((const unsigned char *)&pilot_fixture_api_hash, sizeof pilot_fixture_api_hash, h);
}
/* Test objects use native x86 methods for zero-size surfaces and empty input queues.
 * Original code calls thiscall methods. Translated code calls C adapters.
 * No game instruction is patched.
 */
static uint __attribute__((thiscall)) fixture_method0(void *self) { return 0; }
static uint __attribute__((thiscall)) fixture_method1(void *self, uint a) { ((uint *)self)[1]++; ((uint *)self)[2]=a; return 0; }
static uint __attribute__((thiscall)) fixture_method2(void *self, uint a, uint b) { return 0; }
static uint __attribute__((thiscall)) fixture_method3(void *self, uint a, uint b, uint c) { return 0; }
static uint __attribute__((thiscall)) fixture_rectangle(void *self, uint out) {
    memset((void *)out, 0, 4*sizeof(uint)); return out;
}
static uint fixture_rectangle_adapter(void *self, uint out) { return fixture_rectangle(self, out); }
static uint fixture_adapter0(void *self) { return fixture_method0(self); }
static uint fixture_adapter1(void *self, uint a) { return fixture_method1(self, a); }
static uint fixture_adapter2(void *self, uint a, uint b) { return fixture_method2(self, a, b); }
static uint fixture_adapter3(void *self, uint a, uint b, uint c) { return fixture_method3(self, a, b, c); }
void *pilot_fixture_resolve(uint a) {
    if (a == (uint)fixture_rectangle) return fixture_rectangle_adapter;
    if (a == (uint)fixture_method0) return fixture_adapter0;
    if (a == (uint)fixture_method1) return fixture_adapter1;
    if (a == (uint)fixture_method2) return fixture_adapter2;
    if (a == (uint)fixture_method3) return fixture_adapter3;
    return 0;
}
static void prepare_startup_case(uint addr, int s, uint scratch, uint args[6]) {
    memset((void *)scratch, 0, 0x7000);
    uint *table = (uint *)(scratch + 0x4000);
    for (int i = 0; i < 64; i++) table[i] = (uint)fixture_method0;
    table[1] = (uint)fixture_method3;
    table[0x18/4] = (uint)fixture_method1;
    table[0x78/4] = (uint)fixture_rectangle;
    table[0x4c/4] = (uint)fixture_method2;
    *(uint *)(scratch + 0x2000) = (uint)table;
    uint *queue_table=(uint *)(scratch+0x4400);
    uint *screen_table=(uint *)(scratch+0x4800);
    memcpy(queue_table,table,64*sizeof(uint));memcpy(screen_table,table,64*sizeof(uint));
    queue_table[0x40/4]=(uint)fixture_method2;
    queue_table[0x3c/4]=(uint)fixture_method2;
    screen_table[0x40/4]=(uint)fixture_method1;
    screen_table[0x48/4]=(uint)fixture_method2;
    *(uint *)(scratch + 0x3000) = (uint)queue_table;
    *(uint *)0x839954 = scratch + 0x2000;
    *(uint *)0x839c68 = scratch + 0x3000;
    *(uint *)0x8324e0 = (uint)screen_table;
    *(uint *)0x832468 = scratch + 0x6000;
    *(uint *)0x831468 = scratch + 0x1000;
    *(uint *)0x83996c = 0;
    *(uint *)0x8399b0 = 0;
    *(uint *)0xa40eec = 0;
    *(unsigned char *)0xabcd85 = 0;
    *(unsigned char *)0xa3d8c4 = 0;
    *(uint *)0xb263d8 = 0;
    *(uint *)0xad0440 = 0;
    *(unsigned char *)0xac119c = 1; /* Empty shell artwork is already prepared. */
    *(uint *)0x8523e0 = 640;
    *(uint *)0x8523e4 = 480;
    static const uint positions[] = {0xac11c0,0xac11c4,0xac11c8,0xac11b4,0xac11cc,0xac11d0};
    for (unsigned i=0; i<sizeof positions/sizeof positions[0]; i++)
        *(uint *)positions[i] = scratch + 0x6800;
    static const uint artwork[] = {0xac10e8,0xac107c,0xac10b8,0xac1044,0xac1060,0xac10d8,0xac1128,0xac1010};
    for (unsigned i=0; i<sizeof artwork/sizeof artwork[0]; i++) *(uint *)artwork[i] = 0;
    *(uint *)0xac102c = 0;
    *(uint *)0xad0968 = 0;
    *(uint *)0xad095c = 0;
    *(uint *)0xad08c8 = 0;
    *(uint *)0xad07d0 = 0;
    *(uint *)0x830fb8 = 0;
    *(uint *)0x831438 = 0;
    *(uint *)0xaf8fe8 = 0;
    *(uint *)0xa3d298 = s==2 ? 2 : 0;
    if (addr==0x65c040) {
        *(unsigned char *)0xa70fa5 = 1; *(uint *)0xa70f64 = 0;
        *(uint *)0xaf8fe8 = 1; *(uint *)0xaf8fdc = scratch+0x6200;
        *(uint *)(scratch+0x6200)=scratch+0x6300;
        *(uint *)(scratch+0x6300)=scratch+0x6400;
        *(uint *)(scratch+0x6400+0xc)=(uint)fixture_method1;
    }
    *(uint *)(*(uint *)0xa3d290 + 0x614) = s == 0 ? 0xffffffffu : 100u + s;
    *(uint *)(scratch + 0xac) = 77;
    *(uint *)(scratch + 0xb0) = 88;
    *(uint *)(scratch + 0xb4) = 99;
    memset(args, 0, 6*sizeof(uint));
    if (addr == 0x58f900 || addr == 0x64bc10) args[0] = scratch;
    if (addr == 0x6554a0) {
        snprintf((char *)scratch, 64, "fixture%d.sav", s);
        uint16_t *description = (uint16_t *)(scratch + 0x200);
        description[0] = 'T'; description[1] = 0;
        args[0] = scratch; args[1] = scratch + 0x200; args[2] = s;
    }
}

typedef void (__attribute__((thiscall)) *fixture_ctor)(void *);
typedef void (__attribute__((thiscall)) *fixture_string_ctor)(void *, const char *);
typedef uint (__attribute__((thiscall)) *fixture_ini_set)(void *, const char *, const char *, const char *);
static void *fixture_allocate(uint bytes) {
    return (void *)(uint)pilot_call_orig(0x782017,0,0,1,&bytes);
}
static void prepare_ini_case(uint addr, int s, uint scratch, uint args[6]) {
    memset((void *)scratch,0,0x7000);
    *(uint *)0xa3d298=0;
    void *ini=(void *)(scratch+0x1000);
    ((fixture_ctor)0x51ad50)(ini);
    if (addr==0x5b0a20) {
        ((fixture_string_ctor)0x76d920)((void *)(scratch+0x1100),"Test");
        if (s) ((fixture_ini_set)0x50e5f0)(ini,"Test","Scenario",s==1?"map01":"map02");
        if (s==2) {
            ((fixture_ini_set)0x50e5f0)(ini,"Test","VoiceOver","intro.wav");
            ((fixture_ini_set)0x50e5f0)(ini,"Test","MapVQ","intro.vqa");
            ((fixture_ini_set)0x50e5f0)(ini,"Test","Overlays","first,second");
            ((fixture_ini_set)0x50e5f0)(ini,"Test","ClickMap","map.pcx");
            ((fixture_ini_set)0x50e5f0)(ini,"Test","Text1","fixture");
            ((fixture_ini_set)0x50e5f0)(ini,"Test","Targets","1,2,3");
            ((fixture_ini_set)0x50e5f0)(ini,"Test","0","target");
        }
        args[0]=scratch; args[1]=(uint)ini; args[2]=scratch+0x1100;
    } else {
        static const uint counts[]={0x00839b10,0x00839ba0,0x008661f0,0x00a35ce8,0x00a35d00,0x00a35d18,0x00a35d58,0x00a35de0,0x00a35df8,0x00a35e10,0x00a3d288,0x00a40330,0x00a40348,0x00a40360,0x00a40af0,0x00a40bd8};
        for (unsigned i=0;i<sizeof counts/sizeof counts[0];i++) *(uint *)counts[i]=0;
        if (s) {
            ((fixture_ini_set)0x50e5f0)(ini,"Sleep","NoThreat",s==1?"yes":"no");
            uint *obj=(uint *)(scratch+0x2000),*table=(uint *)(scratch+0x3000);
            obj[0]=(uint)table; table[0x64/4]=(uint)fixture_method1;
            *(uint *)(scratch+0x4000)=(uint)obj;
            *(uint *)0xa35d0c=scratch+0x4000; *(uint *)0xa35d18=1;
        }
        args[0]=(uint)ini;
    }
}
static void prepare_cleanup_case(int s,uint scratch,uint args[6]) {
    memset((void *)scratch,0,0x1000);
    args[0]=scratch;
    if (!s) return;
    ((fixture_ctor)0x604120)((void *)scratch);
    ((fixture_ctor)0x604120)((void *)(scratch+0x100));
    *(uint *)0xa76414=scratch+0x100;
    if (s==2) {
        uint *table=*(uint **)(scratch+12);
        char *entry=fixture_allocate(0x104); memset(entry,0,0x104);strcpy(entry,"fixture");
        int bucket=(int)(uint)pilot_call_orig(0x603610,(uint)entry,0,0,0) % (int)table[2];
        uint *vector=(uint *)(table[0]+bucket*0x18);
        vector[1]=(uint)entry;vector[2]=1;vector[4]=1;
        ((unsigned char *)vector)[13]=1;
    }
}

/* Inputs for the five assembly-checked x87 return signatures. */
static int x87_case(uint a) {
    return a == 0x405120 || a == 0x47fcd0 || a == 0x58deb0 ||
           a == 0x69ae40 || a == 0x6d3ea0;
}
static void prepare_x87_case(uint addr, int s, uint scratch, uint *args) {
    static const float fv[] = {0.0f, -0.0f, 1.25f, -1.75f, 0x1p-126f,
        0x1.fffffep+127f, __builtin_inff(), -__builtin_inff(), __builtin_nanf("")};
    static const double dv[] = {0.0, -0.0, 1.25, -1.75, 0x1p-1022,
        0x1.fffffffffffffp+1023, __builtin_inf(), -__builtin_inf(), __builtin_nan("")};
    memset(args, 0, 16 * sizeof *args);
    memset((void *)scratch, 0, 0x200);
    args[0] = scratch;
    if (addr == 0x405120) {
        *(uint *)0x830fb8 = s != 0;
        memcpy((void *)(scratch+0x54), &fv[(s+8)%9], sizeof(float));
    } else if (addr == 0x47fcd0) {
        static const int values[] = {0, 1, -1, 3, -7, 2147483647, (-2147483647-1), 12345, -12345};
        *(int *)(*(uint *)0x839848 + 0x1320) = values[s%9];
    } else if (addr == 0x58deb0) {
        *(uint *)(scratch+4) = scratch+0x100;
        memcpy((void *)(scratch+0x100), dv, sizeof dv);
        args[1] = s%9;
    } else if (addr == 0x69ae40) {
        memcpy((void *)scratch, fv, sizeof fv);
        args[1] = s%9;
    }
}

static const char *dump_path;
static void report(int kind, uint value)
{
    if (dump_path) {   /* debug: write the compared memory for an offline diff */
        FILE *f = fopen(dump_path, "wb");
        fwrite((void *)0x7c9000, 1, 0xb2d000 - 0x7c9000, f); fwrite(g_objs, 1, g_objs_len, f); fclose(f);
    }
    struct outcome o = { kind, value, kind == K_RET || kind == K_FAULT ? state_hash() : 0, pilot_stub_calls };
    memcpy(o.fp, return_fp, sizeof o.fp);
    if (write(out_fd, &o, sizeof o) != sizeof o) _exit(3);
    _exit(0);
}
static void on_fault(int s, siginfo_t *si, void *uc) {
    if (getenv("DIFF_TRACE")) fprintf(stderr,"[diff] fault ip=%08x address=%08x\n",
        (uint)((ucontext_t *)uc)->uc_mcontext.gregs[REG_EIP], (uint)si->si_addr);
    report(s == SIGALRM ? K_TIMEOUT : K_FAULT, (uint)si->si_addr);
}
static void on_abort(int s) { report(K_ABORT, 0); }

static struct outcome run_child(const struct diff_entry *e, const uint *args, int original)
{
    int pfd[2]; struct outcome o = {0};
    if (pipe(pfd)) return o;
    pid_t pid = fork();
    if (pid == 0) {
        close(pfd[0]); out_fd = pfd[1];
        struct sigaction sa = {0}; sa.sa_sigaction = on_fault; sa.sa_flags = SA_SIGINFO | SA_NODEFER;
        sigaction(SIGSEGV, &sa, 0); sigaction(SIGBUS, &sa, 0); sigaction(SIGFPE, &sa, 0);
        sigaction(SIGILL, &sa, 0); sigaction(SIGALRM, &sa, 0);
        signal(SIGABRT, on_abort);
        pilot_quiet = 1;
        if (getenv("DIFF_ONLY")) dump_path = original ? "/tmp/dt_orig.bin" : "/tmp/dt_tran.bin";
        if (!getenv("DIFF_TRACE")) { int fd = open("/dev/null", 1); dup2(fd, 2); }
        alarm(getenv("RC_ALARM") ? atoi(getenv("RC_ALARM")) : 2);
        uint r = 0;
        /* Lifted code enters through rc_entry_tramp from the SAME call site, so it gets
         * the same stack addresses (and the same old stack contents) as the original. */
        extern void rc_entry_tramp(void); extern uint rc_entry_addr;
        uint target = e->addr;
        if (!original) { rc_entry_addr = e->addr; target = (uint)rc_entry_tramp; }
        int nr = e->nreg;
        if (e->fret) {
            long double result = pilot_call_orig_f(target, nr > 0 ? args[0] : 0, nr > 1 ? args[1] : 0,
                e->nparams - nr, args + nr);
            memcpy(return_fp, &result, sizeof return_fp);
        } else {
            r = (uint)pilot_call_orig(target, nr > 0 ? args[0] : 0, nr > 1 ? args[1] : 0, e->nparams - nr, args + nr);
        }
        uint mask = e->rsize == 0 ? 0 : e->rsize == 1 ? 0xff : e->rsize == 2 ? 0xffff : 0xffffffff;
        report(K_RET, r & mask);
    }
    close(pfd[1]);
    if (read(pfd[0], &o, sizeof o) != sizeof o) o.kind = K_ABORT;
    close(pfd[0]);
    waitpid(pid, 0, 0);
    return o;
}

/* objs: [0] = House built by the original constructor, then scratch buffers */
int pilot_difftest(unsigned char *objs, size_t objs_len, uint house, uint scratch)
{
    g_objs = objs; g_objs_len = objs_len;
    const char *kn[] = { "?", "ret", "fault", "timeout", "abort" };
    int n = 0, match = 0, match_live = 0, mism = 0, mism_stub = 0, input_sets = 0;
    FILE *log = fopen("difftest.log", "w");
    const char *only = getenv("DIFF_ONLY");
    for (const struct diff_entry *e = diff_table; e->addr; e++) {
        if (only && e->addr != strtoul(only, 0, 16)) continue;
        uint sets[3][16] = {
            { house, 0, 0, 0, 0, 0 },
            { house, 1, 1, 1, 1, 1 },
            { house, scratch, scratch, scratch, scratch, scratch },
        };
        int fm = 1, live = 0, stub = 0;
        int map_case = e->addr == 0x405bf0 || e->addr == 0x40a6c0 ||
                       e->addr == 0x40e160 || e->addr == 0x40ff60 ||
                       e->addr == 0x40ff90 || e->addr == 0x428a90 ||
                       e->addr == 0x428ac0 || e->addr == 0x42add0 ||
                       e->addr == 0x56dba0 || e->addr == 0x5950d0;
        int count = x87_case(e->addr) ? 10 : map_case ? 9 : 3;
        for (int s = 0; s < count; s++) {
            uint args[16];
            memcpy(args, sets[s % 3], sizeof args);
            if (map_case) {
                /* Signed coordinates and scalar globals. No map allocation is needed. */
                static const short xs[] = {0, 1, -1, -32768, 32767, 8, -2, 255, -255};
                static const short ys[] = {0, -1, 1, 32767, -32768, 9, 300, -128, 128};
                static const int widths[] = {1, 2, 7, 64, 511, -3, 0, 1024, 9};
                memset((void *)scratch, 0xa5, 0x300);
                short *point = (short *)scratch;
                point[0] = xs[s]; point[1] = ys[s];
                *(int *)0x8325d0 = widths[s];
                *(int *)0x8325d4 = ys[s];
                *(int *)0x84e804 = widths[s];
                *(uint *)0xa70a24 = s & 1 ? 0xfffffff0u : 0x10000000u;
                *(uint *)0x8315ac = s & 1 ? 0xffffffffu : (uint)widths[s];
                *(int *)(scratch + 0x98) = widths[s];
                short *source = (short *)(scratch + 0x200);
                source[0] = ys[s]; source[1] = xs[s];
                args[0] = scratch;
                args[1] = scratch + 0x100;
                args[2] = scratch + 0x200;
            }
            if (e->addr == 0x58f510 || e->addr == 0x58f900 || e->addr == 0x634a00 ||
                e->addr == 0x64bc10 || e->addr == 0x65c140 || e->addr == 0x6554a0 || e->addr == 0x65c040)
                prepare_startup_case(e->addr, s, scratch, args);
            if (e->addr == 0x4e5660 || e->addr == 0x4e5a70) {
                /* Both arguments are real House objects, built by the original constructor. */
                *(int *)(house + 0x30) = 0; *(int *)(scratch + 0x30) = 1;
                *(uint *)(house + 0x5674) = s == 1 ? 2 : 0;
                *(uint *)(scratch + 0x5674) = s == 1 ? 1 : 0;
                *(uint *)(house + 0x120) = 0xa5a5a5a5u;
                *(uint *)(scratch + 0x120) = 0x5a5a5a5au;
                *(uint *)0xa3d298 = 0; *(uint *)0xa40774 = 1;
                *(uint *)0xa40c30 = 0; *(uint *)0x832480 = 0;
                args[0] = house; args[1] = s == 0 ? house : scratch; args[2] = s & 1;
            }
            if (e->addr == 0x5b0a20 || e->addr == 0x652270)
                prepare_ini_case(e->addr,s,scratch,args);
            if (e->addr == 0x604250) prepare_cleanup_case(s,scratch,args);
            if (x87_case(e->addr)) prepare_x87_case(e->addr, s, scratch, args);
            if (pilot_batch_prepare) pilot_batch_prepare(e->addr,s,house,scratch,args);
            input_sets++;
            struct outcome a = run_child(e, args, 1), b = run_child(e, args, 0);
            int same = a.kind == K_RET && b.kind == K_RET && !a.stubs && !b.stubs
                       && a.hash == b.hash && a.value == b.value
                       && !memcmp(a.fp, b.fp, sizeof a.fp);
            if (a.kind == K_RET) live = 1;
            if (b.stubs) stub = 1;
            if (!same) {
                if (e->fret) {
                    fprintf(log, "%08x set%d x87 original=", e->addr, s);
                    for (int j=9; j>=0; j--) fprintf(log, "%02x", a.fp[j]);
                    fprintf(log, " translated=");
                    for (int j=9; j>=0; j--) fprintf(log, "%02x", b.fp[j]);
                    fprintf(log, "\n");
                }
                fm = 0;
                fprintf(log, "%08x set%d MISMATCH original=%s/%08x translated=%s/%08x hash %s stubs=%d\n", e->addr, s,
                        kn[a.kind], a.value, kn[b.kind], b.value, a.hash == b.hash ? "same" : "differs", b.stubs);
            }
        }
        n++;
        if (fm) { match++; if (live) match_live++; }
        else if (stub) mism_stub++;
        else mism++;
        fprintf(log, "%08x %s%s\n", e->addr, fm ? "MATCH" : "MISMATCH", fm && live ? " (returned normally)" : "");
    }
    fclose(log);
    printf("difftest: %d functions, %d input sets: %d match (%d of them ran to a normal return), "
           "%d mismatch, %d mismatch where an untranslated stub was hit\n", n, input_sets, match, match_live, mism, mism_stub);
    return mism + mism_stub + (n == 0);
}
