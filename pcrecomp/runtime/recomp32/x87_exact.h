/* Exact i386 x87 execution. All ten payload bytes survive each instruction.
 * The image preserves TOP, tags, condition codes and sticky exceptions.
 * The compiler sees an empty host stack between guest instructions.
 * Host helpers can still exchange long double values through g_st. */
#ifndef RECOMP_X87_EXACT_H
#define RECOMP_X87_EXACT_H
#define RECOMP_EXACT_X87 1
#if !defined(__i386__) || !defined(__GNUC__)
#error Exact x87 requires GCC-compatible i386 code generation
#endif
#if defined(RECOMP_X87_RING) || defined(RECOMP_X87_LOCAL)
#error Exact x87 requires the shared eight-slot stack
#endif
struct x87_image {
    uint16_t cw, r0, sw, r1, tw, r2;
    uint32_t ip; uint16_t cs, opcode;
    uint32_t dp; uint16_t ds, r3;
    unsigned char st[8][10];
} __attribute__((packed));
_Static_assert(sizeof(struct x87_image) == 108, "i386 FSAVE image");
_Static_assert(__LDBL_MANT_DIG__ == 64, "80-bit long double required");
RECOMP_SHARED_DATA struct x87_image g_x87 = {.cw=0x37f, .tw=0xffff};
static inline void x87_prepare(void) {
    g_x87.cw = g_fpu_cw;
    for (int i=0; i<8; ++i) __builtin_memcpy(g_x87.st[i], &g_st[i], 10);
}
static inline void x87_finish(void) {
    g_fpu_cw = g_x87.cw;
    int top = (g_x87.sw >> 11) & 7;
    g_fp_top = 0;
    for (int i=0; i<8; ++i) {
        __builtin_memcpy(&g_st[i], g_x87.st[i], 10);
        if (((g_x87.tw >> (2*((top+i)&7))) & 3) != 3) ++g_fp_top;
    }
}
/* No host floating-point expression or call may run inside a region.
 * The generator closes it before every label, branch, call and unknown line. */
static inline void x87_batch_begin(void) {
    x87_prepare();
    __asm__ volatile("frstor %0" : "+m"(g_x87) : : "memory",
                     "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)");
}
static inline void x87_batch_end(void) {
    __asm__ volatile("fnsave %0; fldcw %0" : "+m"(g_x87) : : "memory",
                     "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)");
    x87_finish();
}
static inline void x87_external_push(void) {
    int top = (((g_x87.sw >> 11)&7)-1)&7;
    g_x87.sw = (g_x87.sw & ~0x3800) | (top<<11);
    g_x87.tw &= ~(3u << (2*top));
}
static inline void x87_external_pop(void) {
    int top = (g_x87.sw >> 11)&7;
    g_x87.tw |= 3u << (2*top);
    g_x87.sw = (g_x87.sw & ~0x3800) | (((top+1)&7)<<11);
}
#endif
