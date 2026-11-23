/* bridge.S (runtime v2): edges between lifted code and native x86 code.
 *
 * rc_native_asm(RCAux *a): run native code at a->target with the registers in
 *   a->save. The guest stack is real memory: the slot at save[esp] holds the return
 *   address that lifted code pushed; we put the host return label there (C puts the
 *   game value back) and enter the target with "ret". x87 values a->fin move to the
 *   real FPU and the callee's results come back in a->fout. a->host_esp is pushed and
 *   set, so a nested entry (native code calling lifted code) runs below this frame.
 *
 * rc_entry_common: native code called a lifted function through an entry thunk.
 *   In: ecx = this thread's RCAux (entry address in a->entry), native ecx in TMP.
 *   It saves the registers, moves to the host stack and calls rc_entry_c(a).
 *
 * TMP and AUX: thread-local words read with no free register.
 *   Windows: TEB TLS slots (%fs), offsets patched at start-up (rc_patch_*).
 *   Linux tests: one thread, plain globals.
 *
 * RCAux offsets: save 0..28, host_esp 32, host_top 36, target 40, sw_before 44,
 *   fin_n 48, fout_n 52, entry 56, ret 60, eflags 68, fin 80, fout 208 (see rc.h). */
#ifdef _WIN32
# define S(x) _##x
# define LD_AUX(r, n) movl %fs:0x7ff0, r ; rcp_aux_##n:
# define ST_TMP(r, n) movl r, %fs:0x7ff4 ; rcp_tmp_##n:
# define LD_TMP(r, n) movl %fs:0x7ff4, r ; rcp_tmp_##n:
#else
# define S(x) x
# define LD_AUX(r, n) movl $rc_aux, r
# define ST_TMP(r, n) movl r, rc_g_tmp
# define LD_TMP(r, n) movl rc_g_tmp, r
#endif

        .text
        .globl S(rc_native_asm)
S(rc_native_asm):
        push %ebp
        push %ebx
        push %esi
        push %edi
        mov 20(%esp), %ecx
        pushl 32(%ecx)                  /* outer host_esp */
        mov %esp, 32(%ecx)
        fnstsw %ax
        mov %eax, 44(%ecx)
        mov 48(%ecx), %ebx              /* push guest x87 values, deepest first */
        lea 80(%ecx), %edx
1:      test %ebx, %ebx
        jz 2f
        fldt (%edx)
        add $16, %edx
        dec %ebx
        jmp 1b
2:      mov 16(%ecx), %esp              /* guest esp: the return-address slot */
        movl $3f, (%esp)
        pushl 40(%ecx)                  /* target, taken by ret */
        mov 0(%ecx), %eax
        mov 8(%ecx), %edx
        mov 12(%ecx), %ebx
        mov 20(%ecx), %ebp
        mov 24(%ecx), %esi
        mov 28(%ecx), %edi
        mov 4(%ecx), %ecx
        cld
        ret
3:      ST_TMP(%ecx, 1)
        LD_AUX(%ecx, 1)
        mov %eax, 0(%ecx)
        mov %edx, 8(%ecx)
        mov %ebx, 12(%ecx)
        mov %esp, 16(%ecx)
        mov %ebp, 20(%ecx)
        mov %esi, 24(%ecx)
        mov %edi, 28(%ecx)
        LD_TMP(%eax, 2)
        mov %eax, 4(%ecx)
        mov 32(%ecx), %esp              /* back to the host stack (mov/pop keep the flags) */
        popl 32(%ecx)
        pushfl                          /* native flags, for FLAGRET callees */
        popl 68(%ecx)
        /* pop real x87 values left by the callee */
        fnstsw %ax
        mov 44(%ecx), %ebx
        shr $11, %ebx
        and $7, %ebx
        shr $11, %eax
        and $7, %eax
        sub %eax, %ebx
        and $7, %ebx
        mov %ebx, 52(%ecx)
        lea 208(%ecx), %edx
4:      test %ebx, %ebx
        jz 5f
        fstpt (%edx)
        add $16, %edx
        dec %ebx
        jmp 4b
5:      pop %edi
        pop %esi
        pop %ebx
        pop %ebp
        ret

        .globl S(rc_entry_common)
S(rc_entry_common):
        mov %eax, 0(%ecx)
        mov %edx, 8(%ecx)
        mov %ebx, 12(%ecx)
        mov %esp, 16(%ecx)
        mov %ebp, 20(%ecx)
        mov %esi, 24(%ecx)
        mov %edi, 28(%ecx)
        LD_TMP(%eax, 3)
        mov %eax, 4(%ecx)
        mov 32(%ecx), %eax              /* nested: continue below the bridge frame */
        test %eax, %eax
        jnz 6f
        mov 36(%ecx), %eax
6:      mov %eax, %esp
        fnsave 336(%ecx)                /* native x87 stack (arguments in ST) -> R in rc_entry_c */
        push %ecx
        call S(rc_entry_c)
        mov %eax, %ecx
        mov 52(%ecx), %ebx              /* push x87 results, deepest first */
        mov %ebx, %eax
        shl $4, %eax
        lea 208(%ecx,%eax), %edx
7:      test %ebx, %ebx
        jz 8f
        sub $16, %edx
        fldt (%edx)
        dec %ebx
        jmp 7b
8:      mov 16(%ecx), %esp
        pushl 60(%ecx)                  /* return address, taken by ret */
        pushl 68(%ecx)                  /* flags (below the return address), taken by popf */
        mov 0(%ecx), %eax
        mov 8(%ecx), %edx
        mov 12(%ecx), %ebx
        mov 20(%ecx), %ebp
        mov 24(%ecx), %esi
        mov 28(%ecx), %edi
        mov 4(%ecx), %ecx
        popfl
        ret

/* rc_entry_tramp: called like original code (Linux difftest). Entry = rc_entry_addr. */
        .globl S(rc_entry_tramp)
S(rc_entry_tramp):
        ST_TMP(%ecx, 4)
        LD_AUX(%ecx, 4)
        jmp S(rc_entry_common)

#ifdef _WIN32
        .data
        .globl _rc_patch_aux, _rc_patch_tmp
_rc_patch_aux: .long rcp_aux_1, rcp_aux_4, 0
_rc_patch_tmp: .long rcp_tmp_1, rcp_tmp_2, rcp_tmp_3, rcp_tmp_4, 0
#else
        .data
        .globl rc_g_tmp
rc_g_tmp: .long 0
        .section .note.GNU-stack,"",@progbits
#endif
