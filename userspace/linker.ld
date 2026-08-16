OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5); /* R-X */
    rodata PT_LOAD FLAGS(4); /* R-- */
    data   PT_LOAD FLAGS(6); /* RW- */
    stack  PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
    /* A currently unused lower-half PML4 slot, isolated from UEFI mappings. */
    . = 0x0000400000000000;

    .text ALIGN(0x1000) :
    {
        *(.text .text.*)
    } :text

    .rodata ALIGN(0x1000) :
    {
        *(.rodata .rodata.*)
    } :rodata

    .data ALIGN(0x1000) :
    {
        *(.data .data.*)
    } :data

    .bss ALIGN(0x1000) (NOLOAD) :
    {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame.*)
    }
}

ASSERT(DEFINED(_start), "userspace entry symbol _start is missing")
