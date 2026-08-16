    .section .text.start, "ax"
    .global _start
    .type _start, @function
    .extern main
    .extern exit
_start:
    andq $-16, %rsp
    call main
    movl %eax, %edi
    call exit
    ud2
    .size _start, .-_start

    .section .note.GNU-stack, "", @progbits
