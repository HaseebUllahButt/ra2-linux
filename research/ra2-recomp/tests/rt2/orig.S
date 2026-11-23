/* "Original" native functions for the runtime v2 test. Each has a lifted twin in rt2.c. */
        .text
        .globl _orig_rec
/* int rec(int n): n == 0 -> 1; else nat_apply(rec, n - 1) * 3 + n  (cdecl) */
_orig_rec:
        mov 4(%esp), %eax
        test %eax, %eax
        jnz 1f
        mov $1, %eax
        ret
1:      push %ebx
        mov %eax, %ebx
        dec %eax
        push %eax
        push $_orig_rec
        call _nat_apply
        add $8, %esp
        lea (%eax,%eax,2), %eax
        add %ebx, %eax
        pop %ebx
        ret
        .globl _orig_thr
/* DWORD WINAPI thr(int *out): sum of rec(k % 6) for k < 20000, stored to *out */
_orig_thr:
        push %esi
        push %edi
        xor %esi, %esi
        xor %edi, %edi
2:      mov %esi, %eax
        xor %edx, %edx
        mov $6, %ecx
        div %ecx
        push %edx
        call _orig_rec
        add $4, %esp
        add %eax, %edi
        inc %esi
        cmp $20000, %esi
        jb 2b
        mov 12(%esp), %eax
        mov %edi, (%eax)
        xor %eax, %eax
        pop %edi
        pop %esi
        ret $4
