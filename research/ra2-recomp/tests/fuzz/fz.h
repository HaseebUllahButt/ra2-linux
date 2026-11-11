/* fz.h: shared declarations for the instruction fuzzer (harness.c and generated code). */
#ifndef FZ_H
#define FZ_H
#include "rc.h"

typedef struct { u8 cf, zf, sf, of, pf; } FZFlags;
extern FZFlags FZF;          /* lifted code flags in and out */
extern u32 FZI[8];           /* input registers (for mask functions) */
extern int FZ_exit;          /* 0 fall-through, 1 branch target, 2 indirect target / return, 3 other */
extern u32 FZ_target, FZ_IND;
/* compare masks, set by the mask function of each example (defaults set by the harness) */
extern u32 FZ_fmask;         /* eflags bits to compare (CF PF ZF SF DF OF) */
extern u32 FZ_rmask[8];      /* register bits to compare */
extern u32 FZ_cmask;         /* x87 condition bits to compare: bit k = Ck */
extern u32 FZ_xmask;         /* x87 exception bits to compare (status word bits 0..5) */
extern u32 FZ_mm_addr, FZ_mm_mask;   /* one memory dword compared only under this mask */

enum {
    K_X87 = 1, K_STRING = 2, K_REP = 4, K_BTMEM = 8, K_DIV = 16, K_IND = 32, K_RET = 64,
    K_BR = 128, K_POPF = 256, K_FLDCW = 512, K_LEAVE = 1024, K_IDIV = 2048,
};
enum { MF_INT, MF_F32, MF_F64, MF_F80, MF_I16, MF_I32, MF_I64, MF_CW };

struct fzex {
    const char *key, *text;
    u32 addr; int len; const char *bytes;
    void (*lift)(void); void (*mask)(void);
    int kind;
    int mem, base, index, scale; u32 disp; int msize, mfmt;
    u32 target; int indreg, btreg, dsize;
};
extern const struct fzex fz_table[];
extern const int fz_n;
#endif
