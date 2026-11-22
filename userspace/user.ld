/* Userspace Linker Script for Loop-OS */
/* ELF executable for x86_64 */

ENTRY(_start)

SECTIONS
{
    /* User virtual address space starts at 0x400000 */
    . = 0x400000;
    
    /* Text section */
    .text : ALIGN(0x1000)
    {
        *(.text .text.*)
        *(.rodata .rodata.*)
    }
    
    /* Data section */
    .data : ALIGN(0x1000)
    {
        *(.data .data.*)
    }
    
    /* BSS section */
    .bss : ALIGN(0x1000)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }
    
    /* Heap starts after BSS */
    . = ALIGN(0x1000);
    __heap_start = .;
    
    /* Stack at high user address */
    . = 0x7FFFFFFF000;
    __stack_top = .;
    
    /* Discard */
    /DISCARD/ :
    {
        *(.eh_frame .eh_frame_hdr)
        *(.comment)
        *(.note.GNU-stack)
    }
}