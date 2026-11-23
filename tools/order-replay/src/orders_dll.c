/* orders.dll - order replay and recorder inside the ORIGINAL game.exe 1.006 under Wine.
 * Load: wine rclaunch.exe game.exe orders.dll   (env: see orders_core.h)
 * Patches (each checked byte for byte first):
 *   0x005158C4 a1 40 0d a4 00        mov eax,[0xa40d40]  -> jmp seed_stub
 *   0x00540643 e8 28 31 0e 00        call 0x623770       -> call queue_stub (it ends with jmp 0x623770)
 *   0x00540684 89 15 2c 0d a4 00     mov [0xa40d2c],edx  -> jmp boundary_stub (it returns to 0x54068a)
 *   0x00540BB5 a3 1c eb a6 00        mov [0xa6eb1c],eax  -> 5 x nop (measured fps stays 20)
 */
#include <windows.h>
#include <stdint.h>

#define G8(a)  (*(volatile uint8_t  *)(uintptr_t)(a))
#define G16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define G32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define S8(a, v)  (G8(a) = (uint8_t)(v))
#define S32(a, v) (G32(a) = (uint32_t)(v))
#define OR_EXIT() do { fflush(NULL); TerminateProcess(GetCurrentProcess(), 0); } while (0)
#include "orders_core.h"

__attribute__((used, noinline)) static void c_seed(void) { core_seed(); }
__attribute__((used, noinline)) static void c_queue(void) { core_pre_queue(); }
__attribute__((used, noinline)) static void c_boundary(void) { if (core_boundary()) OR_EXIT(); }

/* each stub: save flags, registers and the whole FPU/SSE state, call C, restore */
#define SAVE "pushfl\n\tpushal\n\tcld\n\tmovl %esp,%ebp\n\tsubl $528,%esp\n\tandl $-16,%esp\n\tfxsave (%esp)\n\tfninit\n\t"
#define REST "fxrstor (%esp)\n\tmovl %ebp,%esp\n\tpopal\n\tpopfl\n\t"
__attribute__((naked)) static void seed_stub(void)
{ __asm__ volatile(SAVE "call _c_seed\n\t" REST "movl 0xa40d40,%eax\n\tpushl $0x5158c9\n\tret\n\t"); }
__attribute__((naked)) static void queue_stub(void)
{ __asm__ volatile(SAVE "call _c_queue\n\t" REST "pushl $0x623770\n\tret\n\t"); }
__attribute__((naked)) static void boundary_stub(void)
{ __asm__ volatile("movl %edx,0xa40d2c\n\t" SAVE "call _c_boundary\n\t" REST "pushl $0x54068a\n\tret\n\t"); }

static void put(uint32_t va, const unsigned char *want, const unsigned char *code, uint32_t len)
{
    DWORD old;
    if (memcmp((void *)(uintptr_t)va, want, len)) ExitProcess(171);
    if (!VirtualProtect((void *)(uintptr_t)va, len, PAGE_EXECUTE_READWRITE, &old)) ExitProcess(172);
    memcpy((void *)(uintptr_t)va, code, len);
    VirtualProtect((void *)(uintptr_t)va, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)va, len);
}
static void rel(unsigned char *code, unsigned char op, uint32_t va, void *to, uint32_t len)
{
    memset(code, 0x90, len); code[0] = op; *(int32_t *)(code + 1) = (int32_t)((uintptr_t)to - va - 5);
}

static void install(void)
{
    if (!getenv("ORDERS_OUT")) return;
    unsigned char c[8];
    static const unsigned char w_seed[] = { 0xa1, 0x40, 0x0d, 0xa4, 0x00 };
    static const unsigned char w_queue[] = { 0xe8, 0x28, 0x31, 0x0e, 0x00 };
    static const unsigned char w_bound[] = { 0x89, 0x15, 0x2c, 0x0d, 0xa4, 0x00 };
    static const unsigned char w_fps[] = { 0xa3, 0x1c, 0xeb, 0xa6, 0x00 };
    static const unsigned char nop5[] = { 0x90, 0x90, 0x90, 0x90, 0x90 };
    rel(c, 0xe9, 0x5158c4, (void *)seed_stub, 5); put(0x5158c4, w_seed, c, 5);
    rel(c, 0xe8, 0x540643, (void *)queue_stub, 5); put(0x540643, w_queue, c, 5);
    rel(c, 0xe9, 0x540684, (void *)boundary_stub, 6); put(0x540684, w_bound, c, 6);
    put(0x540bb5, w_fps, nop5, 5);
    S32(OR_FPS, 20); S8(OR_FPS_LOW, 0);
    or_init();
    if (or_ready == 1) { fprintf(or_log, "orders.dll: 4 patches installed (original game.exe)\n"); fflush(or_log); }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD why, LPVOID r)
{
    (void)r;
    if (why == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); install(); }
    return TRUE;
}
