/* GDT (Global Descriptor Table) for x86_64 */

#ifndef _KERNEL_ARCH_X86_64_GDT_H
#define _KERNEL_ARCH_X86_64_GDT_H

#include <stddef.h>
#include <stdint.h>

/* GDT entry */
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

/* GDT pointer */
struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* TSS (Task State Structure) */
struct tss {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

/* Initialize GDT */
void gdt_init(void);

/* Load GDT and TSS */
void gdt_load(void);

/* Stack the CPU switches to on a ring 3 -> ring 0 transition (TSS.rsp0) */
void gdt_set_kernel_stack(uint64_t rsp0);

/* Segment selectors */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x18
#define GDT_USER_CODE   0x20

/* Set kernel stack for IST (Interrupt Stack Table) */
void gdt_set_ist_stack(size_t ist_index, void *stack_top);

#endif