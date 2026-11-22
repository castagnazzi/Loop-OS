/* IDT (Interrupt Descriptor Table) for x86_64 */

#ifndef _KERNEL_ARCH_X86_64_IDT_H
#define _KERNEL_ARCH_X86_64_IDT_H

#include <stdint.h>

/* IDT entry */
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

/* IDT pointer */
struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* Interrupt stack frame (pushed by CPU) */
struct interrupt_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

/* Exception stack frame (with error code) */
struct exception_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
    uint64_t error_code;
} __attribute__((packed));

/* IRQ handler type */
typedef void (*irq_handler_t)(struct interrupt_frame *frame);

/* Initialize IDT */
void idt_init(void);

/* Set IDT gate */
void idt_set_gate(uint8_t vector, void *handler, uint16_t selector, uint8_t flags);

/* Register IRQ handler */
void irq_register_handler(uint8_t irq, irq_handler_t handler);

/* Enable/disable interrupts */
static inline void interrupts_enable(void) { asm volatile("sti"); }
static inline void interrupts_disable(void) { asm volatile("cli"); }

/* Send EOI to PIC */
void pic_send_eoi(uint8_t irq);

/* Remap PIC */
void pic_remap(uint8_t offset1, uint8_t offset2);

#endif