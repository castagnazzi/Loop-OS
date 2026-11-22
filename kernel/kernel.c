/* Loop-OS Kernel Main */
/* Entry point after Limine boot */

#include <kernel/limine.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vmm.h>
#include <kernel/arch/x86_64/gdt.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/serial.h>
#include <kernel/arch/x86_64/cpu.h>
#include <kernel/drivers/timer.h>
#include <kernel/drivers/keyboard.h>

/* Limine request structures (populated by bootloader) */
extern volatile struct limine_base_revision_request limine_base_revision_request;
extern volatile struct limine_framebuffer_request limine_framebuffer_request;
extern volatile struct limine_memmap_request limine_memmap_request;
extern volatile struct limine_entry_point_request limine_entry_point_request;
extern volatile struct limine_kernel_file_request limine_kernel_file_request;
extern volatile struct limine_module_request limine_module_request;
extern volatile struct limine_rsdp_request limine_rsdp_request;
extern volatile struct limine_smp_request limine_smp_request;
extern volatile struct limine_hhdm_request limine_hhdm_request;

/* Kernel command line (from Limine) */
static char kernel_cmdline[256] = {0};

/* Early framebuffer info */
struct limine_framebuffer *early_fb = NULL;

/* ---------------------------------------------------------------- */
/* Early panic - before full init */
__attribute__((noreturn)) void kernel_panic_early(const char *msg) {
    serial_write_string("\n*** KERNEL PANIC (early) ***\n");
    serial_write_string(msg);
    serial_write_string("\n");
    for (;;) asm volatile("cli; hlt");
}

/* ---------------------------------------------------------------- */
/* Clear BSS section */
static void clear_bss(void) {
    extern char __bss_start[], __bss_end[];
    memset(__bss_start, 0, __bss_end - __bss_start);
}

/* ---------------------------------------------------------------- */
/* Parse Limine memory map and initialize PMM */
static void init_memory(void) {
    struct limine_memmap_response *mm_resp = limine_memmap_request.response;
    if (!mm_resp) {
        kernel_panic_early("No memory map from Limine");
    }

    kprintf("Memory map (%d entries):\n", mm_resp->entry_count);
    for (size_t i = 0; i < mm_resp->entry_count; i++) {
        struct limine_memmap_entry *entry = mm_resp->entries[i];
        kprintf("  [%zu] base=0x%lx len=0x%lx type=%d\n",
                i, entry->base, entry->length, entry->type);
    }

    /* Initialize physical memory manager */
    pmm_init(mm_resp);
}

/* ---------------------------------------------------------------- */
/* Initialize virtual memory (paging) */
static void init_paging(void) {
    struct limine_hhdm_response *hhdm_resp = limine_hhdm_request.response;
    uint64_t hhdm_offset = hhdm_resp ? hhdm_resp->offset : 0xFFFF800000000000;

    vmm_init(hhdm_offset);
    kprintf("Paging initialized, HHDM offset: 0x%lx\n", hhdm_offset);
}

/* ---------------------------------------------------------------- */
/* Initialize framebuffer */
static void init_framebuffer(void) {
    struct limine_framebuffer_response *fb_resp = limine_framebuffer_request.response;
    if (!fb_resp || fb_resp->framebuffer_count == 0) {
        kprintf("Warning: No framebuffer available\n");
        return;
    }

    struct limine_framebuffer *fb = fb_resp->framebuffers[0];
    early_fb = fb;

    kprintf("Framebuffer: %dx%d @ 0x%lx, pitch=%d, bpp=%d\n",
            fb->width, fb->height, fb->address, fb->pitch, fb->bpp);

    /* TODO: Map framebuffer in higher-half, init console */
}

/* ---------------------------------------------------------------- */
/* Kernel main - called from boot.S (identity-mapped) */
void kernel_main(void) {
    /* Serial port for early logging */
    serial_init();
    serial_write_string("\n=== Loop-OS Kernel Starting ===\n");

    /* Verify Limine base revision */
    if (limine_base_revision_request.revision != 0) {
        kernel_panic_early("Limine base revision not supported");
    }
    kprintf("Limine base revision: supported\n");

    /* Clear BSS */
    clear_bss();

    /* Initialize subsystems */
    init_memory();
    init_paging();

    /* Now we're in higher-half, call higher-half main */
    /* This is a trampoline - real work in kernel_main_higher_half */
    extern void kernel_main_higher_half(void);
    kernel_main_higher_half();
}

/* ---------------------------------------------------------------- */
/* Kernel main - higher half (after paging enabled) */
void kernel_main_higher_half(void) {
    kprintf("\n=== Loop-OS Kernel (Higher Half) ===\n");
    kprintf("Kernel VMA: 0x%lx\n", (uint64_t)&kernel_main_higher_half);

    /* GDT & IDT */
    gdt_init();
    idt_init();
    kprintf("GDT/IDT initialized\n");

    /* Initialize framebuffer */
    init_framebuffer();

    /* Initialize serial (re-init for higher-half) */
    serial_init_higher_half();

    /* CPU detection */
    cpu_detect();

    /* Timer (PIT) */
    timer_init(1000);  /* 1000 Hz */

    /* Keyboard */
    keyboard_init();

    /* TODO: Initialize VFS, drivers, scheduler, userspace */

    kprintf("\nKernel initialization complete!\n");
    kprintf("Dropping to shell...\n");

    /* Idle loop */
    for (;;) {
        asm volatile("hlt");
    }
}