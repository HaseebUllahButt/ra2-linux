/* Runtime v2 test (J6): native code calls lifted code 3+ levels deep (nat_apply is
 * "Windows" calling back), while a second thread runs lifted code at the same time.
 * Lifted twins are written in the lifter's style (LOADREGS/SAVE/rc_call/guest stack). */
#include <windows.h>
#include <stdio.h>
#include "rc.h"
int orig_rec(int n); DWORD WINAPI orig_thr(void *out);
__attribute__((noinline)) int nat_apply(int (*fn)(int), int x) { volatile int k = 7; return fn(x) + k; }

static void f_rec(void)
{
    LOADREGS;
    eax = M32(esp + 4);
    if (eax == 0) { eax = 1; esp += 4; SAVE; return; }
    esp -= 4; M32(esp) = ebx;
    ebx = eax; eax--;
    esp -= 4; M32(esp) = eax;
    esp -= 4; M32(esp) = (u32)(uintptr_t)orig_rec;
    esp -= 4; M32(esp) = 0xdeadbeefu; SAVE; rc_call((u32)(uintptr_t)nat_apply); LOAD;
    esp += 8;
    eax = eax * 3 + ebx;
    ebx = M32(esp); esp += 4;
    esp += 4; SAVE; return;
}
static void f_thr(void)
{
    LOADREGS;
    esp -= 4; M32(esp) = esi; esp -= 4; M32(esp) = edi;
    esi = 0; edi = 0;
    do {
        edx = esi % 6; eax = esi / 6; ecx = 6;
        esp -= 4; M32(esp) = edx;
        esp -= 4; M32(esp) = 0xfeedf00du; SAVE; rc_call((u32)(uintptr_t)orig_rec); LOAD;
        esp += 4; edi += eax; esi++;
    } while (esi < 20000);
    eax = M32(esp + 12); M32(eax) = edi; eax = 0;
    edi = M32(esp); esp += 4; esi = M32(esp); esp += 4;
    esp += 8; SAVE; return;
}
struct rc_fn { u32 addr; void (*fn)(void); };
struct rc_fn rc_table[2];
const int rc_table_n = 2;
const u8 rc_patchable[2] = { 1, 1 };
__attribute__((constructor)) static void fill(void)
{
    struct rc_fn a = { (u32)(uintptr_t)orig_rec, f_rec }, b = { (u32)(uintptr_t)orig_thr, f_thr };
    if (a.addr < b.addr) { rc_table[0] = a; rc_table[1] = b; } else { rc_table[0] = b; rc_table[1] = a; }
}
static int ref_rec(int n) { return n == 0 ? 1 : (ref_rec(n - 1) + 7) * 3 + n; }
extern long rc_lifted_calls, rc_native_calls;
__declspec(dllexport) int rt2_run(void)
{
    int bad = 0, ref = 0;
    for (int k = 0; k < 20000; k++) ref += ref_rec(k % 6);
    for (int n = 0; n < 8; n++) if (orig_rec(n) != ref_rec(n)) { printf("rec(%d) = %d, want %d\n", n, orig_rec(n), ref_rec(n)); bad++; }
    int out[4] = {0}; HANDLE h[4];
    for (int i = 0; i < 4; i++) h[i] = CreateThread(0, 0, orig_thr, &out[i], 0, 0);
    int mine = 0; orig_thr(&mine);
    WaitForMultipleObjects(4, h, TRUE, INFINITE);
    for (int i = 0; i < 4; i++) if (out[i] != ref) { printf("thread %d sum %d, want %d\n", i, out[i], ref); bad++; }
    if (mine != ref) { printf("main sum %d, want %d\n", mine, ref); bad++; }
    printf("rt2: lifted calls %ld, native calls %ld, depth of rec(7) = 8 levels of native->lifted\n", rc_lifted_calls, rc_native_calls);
    return bad;
}
