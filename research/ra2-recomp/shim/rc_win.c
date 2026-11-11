/* rc_win.c: rc.dll start-up inside the original game.exe (Wine or Windows).
 * - Per-thread lifted state: TLS slot AUX holds &rc_aux, slot TMP is scratch for asm.
 * - Entry patches: each switched-on function start gets "jmp thunk"; the thunk enters
 *   the lifted function through rc_entry_common. Off functions keep original bytes and
 *   lifted callers run them as native code.
 * Config (environment):
 *   RC_ON   = none (default) | all | <file>: hex addresses or "lo-hi" ranges, one per line
 *   RC_OFF  = <file>: hex addresses or ranges to keep off (applied after RC_ON)
 *   RC_LOG  = log file (default rc.log in the game folder) */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rc.h"
struct rc_fn { u32 addr; void (*fn)(void); };
extern const struct rc_fn rc_table[];
extern const int rc_table_n;
extern const u8 rc_patchable[];
extern u32 rc_patch_aux[], rc_patch_tmp[];
extern long rc_native_calls, rc_lifted_calls;
void rc_entry_common(void);
void rc_set_off(int idx, int off);
static DWORD slot_aux, slot_tmp;
static FILE *lg;
static u8 *on;
static int n_on, n_patched;

static void logf_(const char *fmt, ...)
{
    if (!lg) return;
    va_list ap; va_start(ap, fmt); vfprintf(lg, fmt, ap); va_end(ap); fflush(lg);
}
static void thread_attach(void)
{
    rc_thread_init();
    TlsSetValue(slot_aux, &rc_aux);
}
static void patch_imm(u32 *list, u32 val)
{
    for (; *list; list++) {
        u8 *p = (u8 *)(uintptr_t)*list - 4; DWORD old;
        VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &old);
        memcpy(p, &val, 4);
        VirtualProtect(p, 4, old, &old);
    }
}
static void mark(u32 lo, u32 hi, int v)
{
    for (int i = 0; i < rc_table_n; i++) if (rc_table[i].addr >= lo && rc_table[i].addr <= hi) on[i] = (u8)v;
}
static void read_set(const char *p, int v)
{
    if (!p || !*p || !strcmp(p, "none")) return;
    if (!strcmp(p, "all")) { mark(0, 0xffffffffu, v); return; }
    FILE *f = fopen(p, "r");
    if (!f) { logf_("cannot open %s\n", p); return; }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        unsigned lo, hi;
        if (line[0] == '#') continue;
        int k = sscanf(line, "%x-%x", &lo, &hi);
        if (k == 1) hi = lo;
        if (k >= 1) mark(lo, hi, v);
    }
    fclose(f);
}
static DWORD WINAPI stats_thread(void *p)
{
    for (int t = 0;; t++) {
        Sleep(2000);
        logf_("t=%ds lifted_calls=%ld native_calls=%ld\n", 2 * (t + 1), rc_lifted_calls, rc_native_calls);
    }
    return 0;
}
static LONG WINAPI veh(EXCEPTION_POINTERS *e)
{
    static int n;
    DWORD c = e->ExceptionRecord->ExceptionCode;
    if (c == 0x406D1388 || c == DBG_PRINTEXCEPTION_C || n >= 40) return EXCEPTION_CONTINUE_SEARCH;
    n++;
    char mod[MAX_PATH] = "?"; HMODULE hm = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)e->ContextRecord->Eip, &hm)) GetModuleFileNameA(hm, mod, sizeof mod);
    u32 eip = e->ContextRecord->Eip, best = 0, bf = 0;      /* which lifted function holds eip */
    for (int i = 0; i < rc_table_n; i++) {
        u32 f = (u32)(uintptr_t)rc_table[i].fn;
        if (f <= eip && f > bf) { bf = f; best = rc_table[i].addr; }
    }
    if (best && eip - bf < 0x10000) logf_("  in lifted f_%08x+%x\n", best, eip - bf);
    logf_("exception %08lx at %08lx %s+%lx (addr %08lx) esp=%08lx depth=%u\n", c, (unsigned long)e->ContextRecord->Eip,
          strrchr(mod, '\\') ? strrchr(mod, '\\') + 1 : mod, (unsigned long)(e->ContextRecord->Eip - (uintptr_t)hm),
          (unsigned long)(e->ExceptionRecord->NumberParameters > 1 ? e->ExceptionRecord->ExceptionInformation[1] : 0),
          (unsigned long)e->ContextRecord->Esp, rc_aux.depth);
    return EXCEPTION_CONTINUE_SEARCH;
}
static void start(void)
{
    const char *lp = getenv("RC_LOG");
    lg = fopen(lp ? lp : "rc.log", "w");
    slot_aux = TlsAlloc(); slot_tmp = TlsAlloc();
    if (slot_aux >= 64 || slot_tmp >= 64) { logf_("TLS slot too high (%lu %lu)\n", slot_aux, slot_tmp); ExitProcess(3); }
    u32 off_aux = 0xe10 + 4 * slot_aux, off_tmp = 0xe10 + 4 * slot_tmp;
    patch_imm(rc_patch_aux, off_aux);
    patch_imm(rc_patch_tmp, off_tmp);
    thread_attach();
    on = calloc(rc_table_n + 1, 1);
    read_set(getenv("RC_ON"), 1);
    read_set(getenv("RC_OFF"), 0);
    /* thunks: mov %ecx,%fs:tmp; mov %fs:aux,%ecx; movl $addr,56(%ecx); jmp rc_entry_common */
    u8 *th = VirtualAlloc(0, (size_t)rc_table_n * 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    DWORD old; u8 *prot_lo = 0, *prot_hi = 0;
    for (int i = 0; i < rc_table_n; i++) {
        rc_set_off(i, !on[i]);
        if (!on[i]) continue;
        n_on++;
        if (!rc_patchable[i]) continue;
        u32 a = rc_table[i].addr;
        u8 *t = th + 32 * i;
        t[0] = 0x64; t[1] = 0x89; t[2] = 0x0d; memcpy(t + 3, &off_tmp, 4);
        t[7] = 0x64; t[8] = 0x8b; t[9] = 0x0d; memcpy(t + 10, &off_aux, 4);
        t[14] = 0xc7; t[15] = 0x41; t[16] = 56; memcpy(t + 17, &a, 4);
        t[21] = 0xe9; u32 rel = (u32)(uintptr_t)rc_entry_common - (u32)(uintptr_t)(t + 26); memcpy(t + 22, &rel, 4);
        u8 *p = (u8 *)(uintptr_t)a;
        if (p < prot_lo || p + 5 > prot_hi) {      /* make the page writable once */
            prot_lo = (u8 *)((uintptr_t)p & ~0xfffu); prot_hi = prot_lo + 0x2000;
            VirtualProtect(prot_lo, 0x2000, PAGE_EXECUTE_READWRITE, &old);
        }
        u32 rel2 = (u32)(uintptr_t)t - (a + 5);
        p[0] = 0xe9; memcpy(p + 1, &rel2, 4);
        n_patched++;
    }
    FlushInstructionCache(GetCurrentProcess(), 0, 0);
    AddVectoredExceptionHandler(1, veh);
    logf_("rc.dll: %d lifted functions, %d on, %d entry patches, tls %lu %lu\n", rc_table_n, n_on, n_patched, slot_aux, slot_tmp);
    if (!getenv("RC_NOSTATS")) CreateThread(0, 0, stats_thread, 0, 0, 0);
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID p)
{
    if (reason == DLL_PROCESS_ATTACH) start();
    else if (reason == DLL_THREAD_ATTACH) thread_attach();
    return TRUE;
}
