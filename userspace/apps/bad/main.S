    .section .text, "ax"
    .global _start
    .type _start, @function
_start:
    /* Deliberate userspace page fault. The kernel must survive it. */
    xorq %rax, %rax
    movq (%rax), %rbx
    ud2
    .size _start, .-_start

    .section .note.GNU-stack, "", @progbits
