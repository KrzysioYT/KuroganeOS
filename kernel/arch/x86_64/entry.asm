/* entry.asm - x86_64 position-independent kernel entry point */
    .section .text._start, "ax"
    .globl _start
    .extern kmain

_start:
    cli
    leaq stack_top(%rip), %rsp
    andq $-16, %rsp
    xorq %rbp, %rbp
    call kmain

1:
    hlt
    jmp 1b

    .bss
    .align 16
.global kernel_stack_bottom
stack_bottom:
kernel_stack_bottom:
    .skip 65536
.global kernel_stack_top
stack_top:
kernel_stack_top:

    .section .note.GNU-stack, "", @progbits
