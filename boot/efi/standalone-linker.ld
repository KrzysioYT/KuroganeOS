OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(efi_main)

SECTIONS
{
    . = 0x1000;
    __image_start = .;

    .image :
    {
        KEEP(*(.text.efi_main))
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.data .data.*)
        *(.bss .bss.*)
        *(COMMON)
    }

    __image_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame.*)
        *(.got .got.*)
    }
}

ASSERT(efi_main == 0x1000, "UEFI entry must be the first byte in the image")
