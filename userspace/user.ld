/* Linker script for Loop-OS user programs (static ELF, loaded at 4 MiB).
 * Every segment starts on its own page so the kernel can give each its own
 * permissions (text R+X, rodata R, data/bss R+W). */

ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text : ALIGN(0x1000)
    {
        *(.text .text.*)
    }

    .rodata : ALIGN(0x1000)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(0x1000)
    {
        *(.data .data.*)
    }

    .bss : ALIGN(0x1000)
    {
        *(.bss .bss.*)
        *(COMMON)
    }

    /DISCARD/ :
    {
        *(.eh_frame .eh_frame_hdr)
        *(.comment)
        *(.note .note.*)
    }
}
