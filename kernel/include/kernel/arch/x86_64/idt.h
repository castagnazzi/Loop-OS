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

/* Full trap frame as built by isr_common / irq_common (interrupts.S):
 * general registers (pushed last = lowest address), then the vector and
 * error code pushed by the stubs, then the CPU-pushed interrupt frame. */
struct trap_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
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

/* Critical sections: disable interrupts, remembering whether they were on */
static inline uint64_t interrupts_save(void) {
    uint64_t flags;
    asm volatile("pushfq; cli; popq %0" : "=r"(flags) : : "memory");
    return flags;
}
static inline void interrupts_restore(uint64_t flags) {
    if (flags & 0x200) asm volatile("sti" : : : "memory");
}

/* Send EOI to PIC */
void pic_send_eoi(uint8_t irq);

/* Unmask (enable) one IRQ line on the PIC. All lines start masked. */
void pic_unmask(uint8_t irq);

/* Remap PIC */
void pic_remap(uint8_t offset1, uint8_t offset2);

#endif