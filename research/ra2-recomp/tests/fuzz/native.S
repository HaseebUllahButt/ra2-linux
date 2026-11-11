/* native.S: run one real instruction with a given CPU state.
 * fz_native(): load fz_nin (regs, eflags, x87 image), jump to fz_code.
 * The instruction ends in a stub "jmp *fz_after_ptr[k]" (k = 0 fall-through,
 * 1 branch target, 2 indirect target). The stubs save everything into fz_nout. */
        .text
        .globl fz_native
fz_native:
        push %ebp
        push %ebx
        push %esi
        push %edi
        mov %esp, fz_host_esp
        frstor fz_nin_fpu
        pushl fz_nin_flags
        popfl
        mov fz_nin+0, %eax
        mov fz_nin+4, %ecx
        mov fz_nin+8, %edx
        mov fz_nin+12, %ebx
        mov fz_nin+20, %ebp
        mov fz_nin+24, %esi
        mov fz_nin+28, %edi
        mov fz_nin+16, %esp
        jmp *fz_code

        .globl fz_after0, fz_after1, fz_after2
fz_after0:
        movl $0, fz_nout_exit
        jmp 1f
fz_after1:
        movl $1, fz_nout_exit
        jmp 1f
fz_after2:
        movl $2, fz_nout_exit
1:      mov %esp, fz_nout+16
        mov fz_host_esp, %esp
        pushfl
        popl fz_nout_flags
        mov %eax, fz_nout+0
        mov %ecx, fz_nout+4
        mov %edx, fz_nout+8
        mov %ebx, fz_nout+12
        mov %ebp, fz_nout+20
        mov %esi, fz_nout+24
        mov %edi, fz_nout+28
        fnsave fz_nout_fpu
        cld
        pop %edi
        pop %esi
        pop %ebx
        pop %ebp
        ret

        .data
        .globl fz_after_ptr
fz_after_ptr:
        .long fz_after0, fz_after1, fz_after2
        .section .note.GNU-stack,"",@progbits
