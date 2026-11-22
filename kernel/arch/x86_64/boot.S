/* Loop-OS Kernel Entry Point for x86_64 */
/* Compatible with Limine Boot Protocol */

.section .text
.code64
.global _start
.type _start, @function

/* Limine boot protocol requests */
.section .limine_requests, "aw", @progbits
.align 8

/* Limine base revision (required) */
.global limine_base_revision_request
limine_base_revision_request:
    .quad 0xF9562B2D5C95A6C8  /* ID low */
    .quad 0x6A7B384944536BDC  /* ID high */
    .quad 3                   /* requested revision; bootloader zeroes it if supported */

/* Every request: 4-quad ID (2 common magic + 2 specific), revision, response
 * pointer (filled in by Limine), then request-specific extra fields.
 * IDs taken from the official limine.h (v8.x). */
.macro LIMINE_REQUEST name, id_lo, id_hi
.global \name
\name:
    .quad 0xc7b1dd30df4c8b88, 0x0a82e883a194f07b   /* LIMINE_COMMON_MAGIC */
    .quad \id_lo, \id_hi
    .quad 0                   /* revision */
    .quad 0                   /* response pointer */
.endm

/* Framebuffer request */
LIMINE_REQUEST limine_framebuffer_request, 0x9d5827dcd881dd75, 0xa3148604f6fab11b

/* Memory map request */
LIMINE_REQUEST limine_memmap_request, 0x67cf3d9d378a806f, 0xe304acdfc50c3c62

/* Entry point request (+ entry point address).
 * Must point at _start: kernel_main does the full init (serial, memory,
 * paging, GDT/IDT) and then calls kernel_main_higher_half itself. */
LIMINE_REQUEST limine_entry_point_request, 0x13d86c035a1cd3e1, 0x2b0caa89d8f3026a
    .quad _start

/* Kernel file request */
LIMINE_REQUEST limine_kernel_file_request, 0xad97e90e83f1ed67, 0x31eb5d1c5ff23b69

/* Module request (+ internal_module_count, internal_modules) */
LIMINE_REQUEST limine_module_request, 0x3e7e279702be32af, 0xca1c4f3bd1280cee
    .quad 0
    .quad 0

/* RSDP request (ACPI) */
LIMINE_REQUEST limine_rsdp_request, 0xc5e77b6b397e7b43, 0x27637845accdcf3c

/* SMP request (+ flags) */
LIMINE_REQUEST limine_smp_request, 0x95a67b819a1b857e, 0xa0b61b723b6a73e0
    .quad 0

/* HHDM request (Higher Half Direct Map) */
LIMINE_REQUEST limine_hhdm_request, 0x48dcf1cb8ad2b852, 0x63984e959a98244b

/* Stack size for early boot (16 KiB) */
.section .bss
.align 16
.global boot_stack
boot_stack:
    .skip 16384
boot_stack_top:

/* Early boot entry point (called by Limine) */
.section .text
_start:
    /* We're in long mode, identity-mapped low memory */
    /* Stack is already set up by Limine at boot_stack_top */
    
    /* Clear BSS - will be done in C kernel_main */
    /* Save boot info pointers from Limine */
    
    /* Keep the SysV ABI stack alignment and give GDB a clean frame end */
    xorl %ebp, %ebp
    andq $-16, %rsp

    /* Call kernel main (C function) */
    call kernel_main
    
    /* Should never return */
    cli
1:  hlt
    jmp 1b

.size _start, . - _start

/* Kernel main entry (called after basic setup) */
.type _start_kernel, @function
_start_kernel:
    /* This is called via limine_entry_point_request */
    /* Stack is valid, we're in higher-half */
    call kernel_main_higher_half
    
    cli
1:  hlt
    jmp 1b

.size _start_kernel, . - _start_kernel