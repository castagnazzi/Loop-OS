/* GDT Implementation for x86_64 */

#include <kernel/arch/x86_64/gdt.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

/* GDT entries (max 8 for now: null, kernel code, kernel data, user code, user data, TSS x2) */
static struct gdt_entry gdt[8];
static struct gdt_ptr gdt_ptr;
static struct tss tss;

/* Set GDT entry */
static void gdt_set_entry(size_t idx, uint64_t base, uint64_t limit, uint8_t access, uint8_t gran) {
    gdt[idx].limit_low = limit & 0xFFFF;
    gdt[idx].base_low = base & 0xFFFF;
    gdt[idx].base_mid = (base >> 16) & 0xFF;
    gdt[idx].access = access;
    gdt[idx].granularity = (limit >> 16) & 0x0F;
    gdt[idx].granularity |= gran & 0xF0;
    gdt[idx].base_high = (base >> 24) & 0xFF;
}

/* Set TSS entry (uses 2 GDT slots) */
static void gdt_set_tss(size_t idx, struct tss *tss) {
    uint64_t base = (uint64_t)tss;
    uint64_t limit = sizeof(struct tss) - 1;
    
    /* Low part */
    gdt[idx].limit_low = limit & 0xFFFF;
    gdt[idx].base_low = base & 0xFFFF;
    gdt[idx].base_mid = (base >> 16) & 0xFF;
    gdt[idx].access = 0x89;  /* Present, DPL=0, Type=9 (64-bit TSS available) */
    gdt[idx].granularity = (limit >> 16) & 0x0F;
    gdt[idx].granularity |= 0x00;  /* 64-bit TSS, not granular */
    gdt[idx].base_high = (base >> 24) & 0xFF;
    
    /* High part (next entry) */
    gdt[idx + 1].limit_low = (base >> 32) & 0xFFFF;
    gdt[idx + 1].base_low = (base >> 48) & 0xFFFF;
    gdt[idx + 1].base_mid = 0;
    gdt[idx + 1].access = 0;
    gdt[idx + 1].granularity = 0;
    gdt[idx + 1].base_high = 0;
}

void gdt_init(void) {
    /* Null descriptor */
    gdt_set_entry(0, 0, 0, 0, 0);
    
    /* Kernel code segment (64-bit) */
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xA0);  /* Present, DPL=0, Code, Read/Exec, 64-bit */
    
    /* Kernel data segment */
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xA0);  /* Present, DPL=0, Data, Read/Write */
    
    /* User code segment (64-bit) */
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xA0);  /* Present, DPL=3, Code, Read/Exec, 64-bit */
    
    /* User data segment */
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xA0);  /* Present, DPL=3, Data, Read/Write */
    
    /* TSS (uses entries 5 and 6) */
    memset(&tss, 0, sizeof(tss));
    gdt_set_tss(5, &tss);
    
    /* GDT pointer */
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint64_t)&gdt;
    
    kprintf("GDT: initialized with %zu entries\n", sizeof(gdt) / sizeof(gdt[0]));
}

void gdt_load(void) {
    /* Load GDT */
    asm volatile("lgdt %0" : : "m"(gdt_ptr));
    
    /* Reload segment registers */
    asm volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "push $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "push %%rax\n"
        "lretq\n"
        "1:\n"
        : : : "rax", "memory"
    );
    
    /* Load TSS */
    asm volatile("ltr %%ax" : : "a"(0x28));  /* Index 5 * 8 = 0x28 */
}

void gdt_set_ist_stack(size_t ist_index, void *stack_top) {
    if (ist_index >= 7) return;
    tss.ist[ist_index] = (uint64_t)stack_top;
}