    .section .text, "ax"
    .global _start
    .type _start, @function
_start:
    /* Deliberately never cooperates. IRQ0 must still take the CPU away. */
    movq $3, %rax
    int $0x80
1:
    pause
    jmp 1b
    .size _start, .-_start

    .section .note.GNU-stack, "", @progbits
