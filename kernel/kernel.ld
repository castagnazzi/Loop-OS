/* Loop-OS Kernel Linker Script for x86_64 */
/* Higher-half kernel at 0xFFFFFFFF80000000 */

ENTRY(_start)

SECTIONS
{
    /* Kernel virtual base address (higher-half) */
    . = 0xFFFFFFFF80000000;
    KERNEL_VMA = .;

    /* Boot section - must be first for Limine entry point */
    .boot :
    {
        *(.limine_requests)
        *(.boot)
    }

    /* Read-only text section */
    .text : ALIGN(0x1000)
    {
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    /* Read-only data */
    .rodata : ALIGN(0x1000)
    {
        *(.rodata .rodata.*)
    }

    /* Read-write data */
    .data : ALIGN(0x1000)
    {
        *(.data .data.*)
    }

    /* BSS - zero-initialized */
    .bss : ALIGN(0x1000)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* Kernel heap starts after BSS */
    . = ALIGN(0x1000);
    __kernel_heap_start = .;

    /* Symbols for kernel memory layout */
    __kernel_vma_start = KERNEL_VMA;
    __kernel_vma_end = .;

    /* Discard sections */
    /DISCARD/ :
    {
        *(.eh_frame .eh_frame_hdr)
        *(.comment)
        *(.note.GNU-stack)
    }
}