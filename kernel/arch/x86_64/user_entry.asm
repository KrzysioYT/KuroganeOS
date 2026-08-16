/* Controlled ring-3 transition and return path for the v1 int 0x80 ABI. */

    .section .text.user_entry, "ax"
    .global x86_64_enter_user
    .type x86_64_enter_user, @function
x86_64_enter_user:
    /* Preserve the SysV callee-saved state on the launching kernel stack. */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, 0(%rdx)

    /* User RFLAGS inherits only IF from the launcher's saved flags. */
    movq 8(%rdx), %rax
    andq $0x202, %rax
    orq $0x2, %rax

    movw $0x1b, %cx
    movw %cx, %ds
    movw %cx, %es

    pushq $0x1b
    pushq %rsi
    pushq %rax
    pushq $0x23
    pushq %rdi
    iretq
    .size x86_64_enter_user, .-x86_64_enter_user

    /* SYS_EXIT changes the interrupt frame to return here at CPL0 with IF
       clear. Move off the TSS entry stack before restoring the launch flags. */
    .global x86_64_interrupt_return_to_kernel
    .type x86_64_interrupt_return_to_kernel, @function
x86_64_interrupt_return_to_kernel:
    cli
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    pushq %rdi
    popfq
    ret
    .size x86_64_interrupt_return_to_kernel, .-x86_64_interrupt_return_to_kernel

    .section .note.GNU-stack, "", @progbits
