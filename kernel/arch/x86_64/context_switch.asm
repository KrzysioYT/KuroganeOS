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

    .section .data.thread_context, "aw"
    .p2align 3
preemptive_boot_rsp:
    .quad 0
preemptive_boot_rflags:
    .quad 0

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
    movq %rsp, preemptive_boot_rsp(%rip)
    pushfq
    popq %rax
    movq %rax, preemptive_boot_rflags(%rip)
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
    movq preemptive_boot_rsp(%rip), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    pushq preemptive_boot_rflags(%rip)
    popfq
    ret
    .size x86_64_thread_return_from_preemptive_run, .-x86_64_thread_return_from_preemptive_run
#endif

    .section .note.GNU-stack, "", @progbits
