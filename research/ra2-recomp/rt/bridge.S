/* rc_native(target): run original x86 code (or a host stand-in) with the lifted
 * register state R. The guest stack is real memory: ESP = R.esp + 4 skips the
 * return-address slot that lifted code already pushed; "call" writes the host
 * return address into that same slot. x87 values in R move to the real FPU and back. */
        .text
        .globl rc_native
rc_native:
        push %ebp
        push %ebx
        push %esi
        push %edi
        mov 20(%esp), %eax
        mov %eax, rc_target
        mov %esp, rc_host_esp
        fnstsw %ax
        mov %eax, rc_sw_before
        /* push guest x87 values (rc_fin_n of them, deepest first) */
        mov rc_fin_n, %ecx
        lea rc_fin, %edx
1:      test %ecx, %ecx
        jz 2f
        fldt (%edx)
        add $16, %edx
        dec %ecx
        jmp 1b
2:      mov R+0, %eax
        mov R+4, %ecx
        mov R+8, %edx
        mov R+12, %ebx
        mov R+20, %ebp
        mov R+24, %esi
        mov R+28, %edi
        mov R+16, %esp
        add $4, %esp
        cld
        call *rc_target
        mov %eax, R+0
        mov %ecx, R+4
        mov %edx, R+8
        mov %ebx, R+12
        mov %esp, R+16
        mov %ebp, R+20
        mov %esi, R+24
        mov %edi, R+28
        mov rc_host_esp, %esp
        /* pop real x87 values left by the callee */
        fnstsw %ax
        mov %eax, rc_sw_after
        mov rc_sw_before, %ecx
        shr $11, %ecx
        and $7, %ecx
        shr $11, %eax
        and $7, %eax
        sub %eax, %ecx
        and $7, %ecx
        mov %ecx, rc_fout_n
        lea rc_fout, %edx
3:      test %ecx, %ecx
        jz 4f
        fstpt (%edx)
        add $16, %edx
        dec %ecx
        jmp 3b
4:      pop %edi
        pop %esi
        pop %ebx
        pop %ebp
        ret
        .section .note.GNU-stack,"",@progbits
/* rc_entry_tramp: called like original code (by pilot_call_orig). Saves the real
 * registers into R, runs the lifted function on a private host stack, then returns
 * R's registers and pushes the lifted x87 results onto the real FPU. */
        .text
        .globl rc_entry_tramp
rc_entry_tramp:
        mov %eax, R+0
        mov %ecx, R+4
        mov %edx, R+8
        mov %ebx, R+12
        mov %esp, R+16
        mov %ebp, R+20
        mov %esi, R+24
        mov %edi, R+28
        mov %esp, rc_entry_esp
        mov $rc_private_stack_top, %esp
        mov (%esp), %esp
        call rc_entry_c
        mov rc_fout_n, %ecx
        lea rc_fout, %edx
        mov %ecx, %eax
        shl $4, %eax
        add %eax, %edx
5:      test %ecx, %ecx
        jz 6f
        sub $16, %edx
        fldt (%edx)
        dec %ecx
        jmp 5b
6:      mov R+0, %eax
        mov R+4, %ecx
        mov R+8, %edx
        mov R+12, %ebx
        mov R+20, %ebp
        mov R+24, %esi
        mov R+28, %edi
        mov rc_entry_esp, %esp
        ret
        .section .note.GNU-stack,"",@progbits
