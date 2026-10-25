/* SysV x86-64 cooperative kernel-thread context switch. */

    .section .text.context_switch, "ax"
    .global x86_64_thread_context_switch
    .type x86_64_thread_context_switch, @function
x86_64_thread_context_switch:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)
    movq (%rsi), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
    .size x86_64_thread_context_switch, .-x86_64_thread_context_switch

#if !defined(KUROGANE_HOST_TEST)
    .extern x86_64_interrupt_restore_frame

    .section .text.context_switch, "ax"
    .global x86_64_thread_start_interrupt_frame
    .type x86_64_thread_start_interrupt_frame, @function
x86_64_thread_start_interrupt_frame:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    /* rdi=frame, rsi=CPU-local PreemptiveReturnState. */
    movq %rsp, 0(%rsi)
    pushfq
    popq %rax
    movq %rax, 8(%rsi)
    movq %rdi, %rsp
    jmp x86_64_interrupt_restore_frame
    .size x86_64_thread_start_interrupt_frame, .-x86_64_thread_start_interrupt_frame

    .global x86_64_thread_resume_interrupt_frame
    .type x86_64_thread_resume_interrupt_frame, @function
x86_64_thread_resume_interrupt_frame:
    cli
    movq %rdi, %rsp
    jmp x86_64_interrupt_restore_frame
    .size x86_64_thread_resume_interrupt_frame, .-x86_64_thread_resume_interrupt_frame

    .global x86_64_thread_return_from_preemptive_run
    .type x86_64_thread_return_from_preemptive_run, @function
x86_64_thread_return_from_preemptive_run:
    cli
    /* rdi=CPU-local PreemptiveReturnState. */
    movq 0(%rdi), %rsp
    movq 8(%rdi), %rax
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    pushq %rax
    popfq
    ret
    .size x86_64_thread_return_from_preemptive_run, .-x86_64_thread_return_from_preemptive_run
#endif

    .section .note.GNU-stack, "", @progbits
