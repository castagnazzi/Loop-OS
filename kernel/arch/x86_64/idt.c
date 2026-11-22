/* IDT Implementation for x86_64 */

#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/gdt.h>
#include <kernel/task.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

/* IDT with 256 entries */
static struct idt_entry idt[256];
static struct idt_ptr idt_ptr;

/* IRQ handlers */
static irq_handler_t irq_handlers[16] = {0};

/* Exception names */
static const char *exception_names[32] = {
    "Division by zero",
    "Debug",
    "Non-maskable interrupt",
    "Breakpoint",
    "Overflow",
    "Bound range exceeded",
    "Invalid opcode",
    "Device not available",
    "Double fault",
    "Coprocessor segment overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack segment fault",
    "General protection fault",
    "Page fault",
    "Reserved",
    "x87 FPU error",
    "Alignment check",
    "Machine check",
    "SIMD FPU exception",
    "Virtualization exception",
    "Control protection exception",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor injection exception",
    "VMM communication exception",
    "Security exception",
    "Reserved"
};

/* Assembly interrupt stubs (defined in interrupts.S) */
extern void *isr_stubs[32];
extern void *irq_stubs[16];

/* Generic ISR handler */
void isr_common_handler(struct trap_frame *tf) {
    uint64_t vector = tf->vector;

    /* A fault in user mode only kills that process; the kernel carries on */
    if (tf->cs & 3) {
        uint64_t cr2 = 0;
        if (vector == 14) asm volatile("mov %%cr2, %0" : "=r"(cr2));
        kprintf("\n[pid %d '%s'] crashed: %s at RIP=0x%lx",
                current_task->pid, current_task->name,
                vector < 32 ? exception_names[vector] : "exception", tf->rip);
        if (vector == 14) kprintf(" (address 0x%lx)", cr2);
        kprintf("\n");
        task_exit(128 + 11);
    }

    kprintf("\n*** CPU EXCEPTION %lu: %s ***\n", vector,
            vector < 32 ? exception_names[vector] : "unknown");
    kprintf("  RIP=0x%lx CS=0x%lx RFLAGS=0x%lx\n", tf->rip, tf->cs, tf->rflags);
    kprintf("  RSP=0x%lx SS=0x%lx Error=0x%lx\n", tf->rsp, tf->ss, tf->error_code);
    kprintf("  RAX=0x%lx RBX=0x%lx RCX=0x%lx RDX=0x%lx\n", tf->rax, tf->rbx, tf->rcx, tf->rdx);
    kprintf("  RSI=0x%lx RDI=0x%lx RBP=0x%lx\n", tf->rsi, tf->rdi, tf->rbp);
    if (vector == 14) {
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        kprintf("  CR2=0x%lx (%s, %s, %s)\n", cr2,
                (tf->error_code & 1) ? "protection violation" : "page not present",
                (tf->error_code & 2) ? "write" : "read",
                (tf->error_code & 4) ? "user" : "kernel");
    }

    /* TODO: Proper exception handling (kill task, etc.) */
    for (;;) asm volatile("cli; hlt");
}

/* Generic IRQ handler */
void irq_common_handler(struct trap_frame *tf) {
    uint8_t irq = (uint8_t)tf->vector;

    if (irq < 16 && irq_handlers[irq]) {
        /* The CPU frame (rip, cs, rflags, rsp, ss) starts at &tf->rip */
        irq_handlers[irq]((struct interrupt_frame *)&tf->rip);
    } else {
        kprintf("Unhandled IRQ %d\n", irq);
    }

    /* Send EOI */
    pic_send_eoi(irq);

    /* Timer tick: the scheduler may switch to another task. This must come
     * after the EOI, because the switch can last until the next tick. */
    if (irq == 0) task_timer_tick(tf);
}

void idt_init(void) {
    /* Clear IDT */
    memset(idt, 0, sizeof(idt));
    
    /* Set up exception handlers (0-31) */
    for (int i = 0; i < 32; i++) {
        uint8_t flags = 0x8E;  /* Present, DPL=0, 64-bit interrupt gate */
        if (i == 3 || i == 4) flags = 0x8F;  /* Breakpoint, Overflow: DPL=3 */
        idt_set_gate(i, isr_stubs[i], 0x08, flags);
    }
    
    /* Set up IRQ handlers (32-47) */
    for (int i = 0; i < 16; i++) {
        idt_set_gate(32 + i, irq_stubs[i], 0x08, 0x8E);
    }

    /* IDT pointer */
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)&idt;

    /* Load IDT */
    asm volatile("lidt %0" : : "m"(idt_ptr));

    /* Remap PIC */
    pic_remap(32, 40);

    kprintf("IDT: initialized with %zu entries\n", sizeof(idt) / sizeof(idt[0]));
}

void idt_set_gate(uint8_t vector, void *handler, uint16_t selector, uint8_t flags) {
    uint64_t addr = (uint64_t)handler;
    idt[vector].offset_low = addr & 0xFFFF;
    idt[vector].selector = selector;
    idt[vector].ist = 0;
    idt[vector].type_attr = flags;
    idt[vector].offset_mid = (addr >> 16) & 0xFFFF;
    idt[vector].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vector].reserved = 0;
}

void irq_register_handler(uint8_t irq, irq_handler_t handler) {
    if (irq < 16) {
        irq_handlers[irq] = handler;
    }
}

/* PIC I/O ports */
#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1

#define PIC_EOI 0x20

/* Port I/O */
static inline void serial_outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t serial_inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    asm volatile("outb %%al, $0x80" : : "a"(0));
}

void pic_remap(uint8_t offset1, uint8_t offset2) {
    /* ICW1: initialize */
    serial_outb(PIC1_CMD, 0x11);
    io_wait();
    serial_outb(PIC2_CMD, 0x11);
    io_wait();
    
    /* ICW2: vector offsets */
    serial_outb(PIC1_DATA, offset1);
    io_wait();
    serial_outb(PIC2_DATA, offset2);
    io_wait();
    
    /* ICW3: cascading */
    serial_outb(PIC1_DATA, 0x04);
    io_wait();
    serial_outb(PIC2_DATA, 0x02);
    io_wait();
    
    /* ICW4: 8086 mode */
    serial_outb(PIC1_DATA, 0x01);
    io_wait();
    serial_outb(PIC2_DATA, 0x01);
    io_wait();
    
    /* Mask every IRQ: drivers unmask theirs with pic_unmask() once their
     * handler is registered. (The masks left by the firmware are unknown.) */
    serial_outb(PIC1_DATA, 0xFF);
    serial_outb(PIC2_DATA, 0xFF);

    kprintf("PIC: remapped to 0x%02x-0x%02x\n", offset1, offset2 + 7);
}

void pic_unmask(uint8_t irq) {
    if (irq >= 16) return;
    if (irq >= 8) {
        /* The slave PIC hangs off IRQ2 of the master */
        serial_outb(PIC1_DATA, serial_inb(PIC1_DATA) & ~(1 << 2));
        serial_outb(PIC2_DATA, serial_inb(PIC2_DATA) & ~(1 << (irq - 8)));
    } else {
        serial_outb(PIC1_DATA, serial_inb(PIC1_DATA) & ~(1 << irq));
    }
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) serial_outb(PIC2_CMD, PIC_EOI);
    serial_outb(PIC1_CMD, PIC_EOI);
}
