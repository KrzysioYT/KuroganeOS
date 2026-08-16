    .section .text, "ax"
    .global _start
    .type _start, @function
_start:
    movq $3, %rax
    int $0x80

    /* Invalid user pointers are rejected instead of faulting the kernel. */
    movq $2, %rax
    movq $1, %rdi
    xorq %rsi, %rsi
    movq $1, %rdx
    int $0x80
    cmpq $-1, %rax
    jne 1f

    movq $2, %rax
    movq $1, %rdi
    leaq message(%rip), %rsi
    movq $(message_end - message), %rdx
    int $0x80
    cmpq $(message_end - message), %rax
    jne 1f

    movq $1, %rax
    xorl %edi, %edi
    int $0x80

1:
    movq $1, %rax
    movl $1, %edi
    int $0x80
    ud2
    .size _start, .-_start

    .section .rodata, "a"
message:
    .ascii "hello from ring3 via SYS_WRITE\n"
message_end:

    .section .note.GNU-stack, "", @progbits
