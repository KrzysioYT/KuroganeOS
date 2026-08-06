OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/*
 * Build an ET_DYN kernel whose linked virtual addresses are image-relative.
 * The first PT_LOAD also covers the ELF and program headers, making address
 * zero the unambiguous load-bias origin used by the UEFI loader.
 */
PHDRS
{
    text    PT_LOAD FILEHDR PHDRS FLAGS(5); /* R-X */
    rodata  PT_LOAD FLAGS(4);               /* R-- */
    data    PT_LOAD FLAGS(6);               /* RW- */
    dynamic PT_DYNAMIC FLAGS(6);            /* RW- */
    stack   PT_GNU_STACK FLAGS(6);          /* RW-, never executable */
}

SECTIONS
{
    /*
     * Leave the first page for the in-memory ELF/program headers.  Every
     * symbol remains within two GiB of every other symbol, as required by the
     * x86-64 small PIE code model.
     */
    . = 0x1000;
    __kernel_start = .;

    .text ALIGN(0x1000) :
    {
        __text_start = .;
        KEEP(*(.text._start))
        *(.text .text.*)
        __text_end = .;
    } :text

    . = ALIGN(0x1000);
    .rodata :
    {
        __rodata_start = .;
        /*
         * GNU ld reserves/zeros the first scalar at this output-section
         * boundary for this PIE layout. Keep the guard explicit so the first
         * real read-only object cannot silently lose its leading field.
         */
        LONG(0);
        __rodata_payload_start = .;
        *(.rodata .rodata.*)
        *(.gcc_except_table .gcc_except_table.*)
        *(.eh_frame_hdr)
        *(.eh_frame)

        /*
         * The loader discovers RELA through PT_DYNAMIC.  Keep all referenced
         * dynamic metadata in a read-only PT_LOAD segment.
         */
        *(.dynsym)
        *(.dynstr)
        *(.hash)
        *(.gnu.hash)
        *(.rela.dyn)
        __rodata_end = .;
    } :rodata

    . = ALIGN(0x1000);
    .data :
    {
        __data_start = .;
        *(.data .data.*)
        *(.got .got.*)
        KEEP(*(.init_array .init_array.*))
        KEEP(*(.fini_array .fini_array.*))
        __data_end = .;
    } :data

    .dynamic :
    {
        *(.dynamic)
    } :data :dynamic

    .bss (NOLOAD) :
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    . = ALIGN(0x1000);
    __kernel_end = .;

    /DISCARD/ :
    {
        *(.interp)
        *(.comment)
        *(.note .note.*)
    }
}

ASSERT(DEFINED(_start), "kernel entry symbol _start is missing")
ASSERT(SIZEOF_HEADERS <= 0x1000, "ELF headers exceed the reserved first page")
ASSERT(ADDR(.text) == 0x1000, ".text must start at image offset 0x1000")
ASSERT((ADDR(.rodata) & 0xFFF) == 0, ".rodata is not page aligned")
ASSERT((ADDR(.data) & 0xFFF) == 0, ".data is not page aligned")
ASSERT(SIZEOF(.dynamic) != 0, "PIE kernel is missing PT_DYNAMIC metadata")
ASSERT(__kernel_end < 0x80000000,
       "kernel exceeds the x86-64 small PIE code-model span")
