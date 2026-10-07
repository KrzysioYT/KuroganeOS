/* x86-64 AP startup trampoline.
 *
 * The BSP copies this bounded blob to physical 0x7000 and patches the mailbox
 * fields before issuing INIT/SIPI.  The kernel keeps the low trampoline page
 * reserved for the lifetime of the SMP runtime.
 */
#define KU_SMP_TRAMPOLINE_BASE 0x7000

    .section .rodata.smp_trampoline, "a", @progbits
    .balign 16
    .global smp_trampoline_start
    .global smp_trampoline_end
    .global smp_trampoline_mailbox_cr3
    .global smp_trampoline_mailbox_stack
    .global smp_trampoline_mailbox_entry
    .global smp_trampoline_mailbox_cpu_index
    .global smp_trampoline_mailbox_apic_id

smp_trampoline_start:
    .code16
    cli
    cld
    movw %cs, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x0ff0, %sp

    lgdt KU_SMP_TRAMPOLINE_BASE + (smp_trampoline_gdt_descriptor - smp_trampoline_start)

    movl %cr0, %eax
    orl $0x00000001, %eax
    movl %eax, %cr0
    ljmpl $0x08, $(KU_SMP_TRAMPOLINE_BASE + (smp_trampoline_pm32 - smp_trampoline_start))

    .code32
smp_trampoline_pm32:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    movl KU_SMP_TRAMPOLINE_BASE + (smp_trampoline_mailbox_cr3 - smp_trampoline_start), %eax
    movl %eax, %cr3

    movl %cr4, %eax
    orl $0x00000020, %eax              /* CR4.PAE */
    movl %eax, %cr4

    movl $0xC0000080, %ecx             /* IA32_EFER */
    rdmsr
    orl $0x00000900, %eax              /* LME | NXE */
    wrmsr

    movl %cr0, %eax
    andl $0xfffffffb, %eax              /* clear EM */
    orl $0x80010023, %eax               /* PG | WP | NE | MP | PE */
    movl %eax, %cr0

    ljmpl $0x18, $(KU_SMP_TRAMPOLINE_BASE + (smp_trampoline_lm64 - smp_trampoline_start))

    .code64
smp_trampoline_lm64:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    movq KU_SMP_TRAMPOLINE_BASE + (smp_trampoline_mailbox_stack - smp_trampoline_start), %rsp
    andq $-16, %rsp
    xorq %rbp, %rbp
    movl KU_SMP_TRAMPOLINE_BASE + (smp_trampoline_mailbox_cpu_index - smp_trampoline_start), %edi
    movl KU_SMP_TRAMPOLINE_BASE + (smp_trampoline_mailbox_apic_id - smp_trampoline_start), %esi
    movq KU_SMP_TRAMPOLINE_BASE + (smp_trampoline_mailbox_entry - smp_trampoline_start), %rax
    jmp *%rax

    .balign 8
smp_trampoline_mailbox_cr3:
    .long 0
    .long 0
smp_trampoline_mailbox_stack:
    .quad 0
smp_trampoline_mailbox_entry:
    .quad 0
smp_trampoline_mailbox_cpu_index:
    .long 0
smp_trampoline_mailbox_apic_id:
    .long 0

    .balign 8
smp_trampoline_gdt:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff       /* 0x08: flat 32-bit code */
    .quad 0x00cf92000000ffff       /* 0x10: flat data */
    .quad 0x00af9a000000ffff       /* 0x18: flat 64-bit code */
smp_trampoline_gdt_end:

smp_trampoline_gdt_descriptor:
    .word smp_trampoline_gdt_end - smp_trampoline_gdt - 1
    .long KU_SMP_TRAMPOLINE_BASE + (smp_trampoline_gdt - smp_trampoline_start)

smp_trampoline_end:
    .code64

    .section .note.GNU-stack, "", @progbits
