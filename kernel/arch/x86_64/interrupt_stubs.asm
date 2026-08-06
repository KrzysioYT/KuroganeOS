/* x86-64 interrupt/exception entry stubs (GNU assembler, AT&T syntax). */

    .altmacro
    .section .text.interrupts, "ax"
    .extern x86_64_interrupt_dispatch

    .macro ISR_NO_ERROR vector
    .global interrupt_stub_\vector
    .type interrupt_stub_\vector, @function
interrupt_stub_\vector:
    pushq $0
    pushq $\vector
    jmp interrupt_common_entry
    .size interrupt_stub_\vector, .-interrupt_stub_\vector
    .endm

    .macro ISR_ERROR vector
    .global interrupt_stub_\vector
    .type interrupt_stub_\vector, @function
interrupt_stub_\vector:
    /* The processor already pushed the exception error code. */
    pushq $\vector
    jmp interrupt_common_entry
    .size interrupt_stub_\vector, .-interrupt_stub_\vector
    .endm

    .macro ISR_TABLE_ENTRY vector
        .quad interrupt_stub_\vector
    .endm

    /* Exceptions without a processor-supplied error code. */
    ISR_NO_ERROR 0
    ISR_NO_ERROR 1
    ISR_NO_ERROR 2
    ISR_NO_ERROR 3
    ISR_NO_ERROR 4
    ISR_NO_ERROR 5
    ISR_NO_ERROR 6
    ISR_NO_ERROR 7
    ISR_ERROR    8
    ISR_NO_ERROR 9
    ISR_ERROR    10
    ISR_ERROR    11
    ISR_ERROR    12
    ISR_ERROR    13
    ISR_ERROR    14
    ISR_NO_ERROR 15
    ISR_NO_ERROR 16
    ISR_ERROR    17
    ISR_NO_ERROR 18
    ISR_NO_ERROR 19
    ISR_NO_ERROR 20
    ISR_ERROR    21
    ISR_NO_ERROR 22
    ISR_NO_ERROR 23
    ISR_NO_ERROR 24
    ISR_NO_ERROR 25
    ISR_NO_ERROR 26
    ISR_NO_ERROR 27
    ISR_NO_ERROR 28
    ISR_ERROR    29
    ISR_ERROR    30
    ISR_NO_ERROR 31

    /* IRQs and software-defined vectors never receive an automatic error. */
    .set stub_number, 32
    .rept 224
        ISR_NO_ERROR %stub_number
        .set stub_number, stub_number + 1
    .endr

    .global interrupt_common_entry
    .type interrupt_common_entry, @function
interrupt_common_entry:
    cld

    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* An interrupt can arrive at any stack alignment. Preserve the exact
       frame pointer in a callee-saved register and align for the SysV call. */
    movq %rsp, %rdi
    movq %rsp, %r12
    andq $-16, %rsp
    call x86_64_interrupt_dispatch
    movq %r12, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp
    iretq
    .size interrupt_common_entry, .-interrupt_common_entry

    /*
     * The pointer table needs R_X86_64_RELATIVE fixups in a PIE kernel.
     * Keep it in the writable load segment so the UEFI loader never has to
     * modify executable or read-only pages.
     */
    .section .data.rel.ro.interrupts, "aw"
    .p2align 3
    .global interrupt_stub_table
    .type interrupt_stub_table, @object
interrupt_stub_table:
    .set table_index, 0
    .rept 256
        ISR_TABLE_ENTRY %table_index
        .set table_index, table_index + 1
    .endr
    .size interrupt_stub_table, .-interrupt_stub_table

    .section .note.GNU-stack, "", @progbits
