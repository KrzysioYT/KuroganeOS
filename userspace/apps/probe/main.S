    .section .text, "ax"
    .global _start
    .type _start, @function
_start:
    xorq %r15, %r15
    movq $3, %rax                  /* getpid */
    int $0x80
    testq %rax, %rax
    jz fail
    incq %r15

    movq $11, %rax                 /* gettid */
    int $0x80
    testq %rax, %rax
    jz fail
    incq %r15

    movq $7, %rax                  /* alloc */
    movq $4096, %rdi
    int $0x80
    testq %rax, %rax
    js fail
    movabsq $0x0000400000000000, %rcx
    cmpq %rcx, %rax
    jb fail
    movq %rax, %r12
    movabsq $0x4b55524f41424931, %rcx
    movq %rcx, (%r12)
    incq %r15

    movq $8, %rax                  /* free */
    movq %r12, %rdi
    int $0x80
    testq %rax, %rax
    jne fail
    movq $8, %rax                  /* stale allocation is rejected */
    movq %r12, %rdi
    int $0x80
    cmpq $-1, %rax
    jne fail
    incq %r15

    movq $5, %rax                  /* open */
    leaq config_path(%rip), %rdi
    movq $(config_path_end - config_path), %rsi
    movq $1, %rdx
    int $0x80
    testq %rax, %rax
    jle fail
    movq %rax, %r13
    incq %r15

    movq $4, %rax                  /* read */
    movq %r13, %rdi
    leaq read_buffer(%rip), %rsi
    movq $128, %rdx
    int $0x80
    testq %rax, %rax
    jle fail
    incq %r15

    movq $6, %rax                  /* close */
    movq %r13, %rdi
    int $0x80
    testq %rax, %rax
    jne fail
    incq %r15

    movq $12, %rax                 /* spawn */
    leaq child_path(%rip), %rdi
    movq $(child_path_end - child_path), %rsi
    int $0x80
    testq %rax, %rax
    jle fail
    movq %rax, %r14
    incq %r15

wait_child:
    movq $13, %rax                 /* wait */
    movq %r14, %rdi
    leaq child_status(%rip), %rsi
    int $0x80
    testq %rax, %rax
    jz child_done
    cmpq $-9, %rax
    jne fail
    movq $10, %rax                 /* yield */
    int $0x80
    jmp wait_child

child_done:
    cmpl $0, child_status(%rip)
    jne fail
    incq %r15
    movq $9, %rax                  /* sleep one timer tick */
    movq $1, %rdi
    int $0x80
    testq %rax, %rax
    jne fail
    incq %r15

    movq $2, %rax
    movq $1, %rdi
    leaq success(%rip), %rsi
    movq $(success_end - success), %rdx
    int $0x80
    cmpq $(success_end - success), %rax
    jne fail

    movq $1, %rax
    xorl %edi, %edi
    int $0x80

fail:
    movq $2, %rax
    movq $2, %rdi
    leaq failure_prefix(%rip), %rsi
    movq $(failure_prefix_end - failure_prefix), %rdx
    int $0x80
    movq $2, %rax
    movq $2, %rdi
    leaq stage_codes(%rip), %rsi
    addq %r15, %rsi
    movq $1, %rdx
    int $0x80
    movq $1, %rax
    movl $1, %edi
    int $0x80
    ud2
    .size _start, .-_start

    .section .rodata, "a"
config_path:
    .ascii "/etc/system.cfg"
config_path_end:
child_path:
    .ascii "/apps/hello"
child_path_end:
success:
    .ascii "userspace ABI probe passed\n"
success_end:
failure_prefix:
    .ascii "userspace ABI probe failed at "
failure_prefix_end:
stage_codes:
    .ascii "PTAFORCSWL?"

    .section .bss, "aw", @nobits
    .p2align 3
child_status:
    .long 0
    .p2align 4
read_buffer:
    .skip 128

    .section .note.GNU-stack, "", @progbits
