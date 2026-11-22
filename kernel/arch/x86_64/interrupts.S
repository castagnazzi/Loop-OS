/* Interrupt Stubs for x86_64 */
/* Generated entry points for ISRs and IRQs */

.section .text
.code64

/* Macro for ISR stubs (exceptions) */
.macro ISR_STUB num, has_error_code=0
.global isr_stub_\num
.type isr_stub_\num, @function
.align 16
isr_stub_\num:
    .if \has_error_code == 0
        pushq $0          /* No error code from CPU - push dummy */
    .endif
    /* else: CPU already pushed the real error code */
    pushq $\num          /* Vector number */
    jmp isr_common
.size isr_stub_\num, . - isr_stub_\num
.endm

/* Macro for IRQ stubs */
.macro IRQ_STUB num
.global irq_stub_\num
.type irq_stub_\num, @function
.align 16
irq_stub_\num:
    pushq $0              /* No error code */
    pushq $\num           /* IRQ number (0-15) */
    jmp irq_common
.size irq_stub_\num, . - irq_stub_\num
.endm

/* ISR Stubs (0-31) - exceptions with error codes: 8, 10-14, 17, 30 */
ISR_STUB 0, 0   /* Division by zero */
ISR_STUB 1, 0   /* Debug */
ISR_STUB 2, 0   /* NMI */
ISR_STUB 3, 0   /* Breakpoint */
ISR_STUB 4, 0   /* Overflow */
ISR_STUB 5, 0   /* Bound range exceeded */
ISR_STUB 6, 0   /* Invalid opcode */
ISR_STUB 7, 0   /* Device not available */
ISR_STUB 8, 1   /* Double fault (has error code) */
ISR_STUB 9, 0   /* Coprocessor segment overrun */
ISR_STUB 10, 1  /* Invalid TSS */
ISR_STUB 11, 1  /* Segment not present */
ISR_STUB 12, 1  /* Stack segment fault */
ISR_STUB 13, 1  /* General protection fault */
ISR_STUB 14, 1  /* Page fault */
ISR_STUB 15, 0  /* Reserved */
ISR_STUB 16, 0  /* x87 FPU error */
ISR_STUB 17, 1  /* Alignment check */
ISR_STUB 18, 0  /* Machine check */
ISR_STUB 19, 0  /* SIMD FPU exception */
ISR_STUB 20, 0  /* Virtualization exception */
ISR_STUB 21, 0  /* Control protection exception */
ISR_STUB 22, 0  /* Reserved */
ISR_STUB 23, 0  /* Reserved */
ISR_STUB 24, 0  /* Reserved */
ISR_STUB 25, 0  /* Reserved */
ISR_STUB 26, 0  /* Reserved */
ISR_STUB 27, 0  /* Reserved */
ISR_STUB 28, 0  /* Hypervisor injection exception */
ISR_STUB 29, 0  /* VMM communication exception */
ISR_STUB 30, 1  /* Security exception */
ISR_STUB 31, 0  /* Reserved */

/* Syscall entry (vector 0x80) */
.global syscall_stub
.type syscall_stub, @function
.align 16
syscall_stub:
    /* Save user registers */
    swapgs
    movq %rsp, %gs:0      /* Save user RSP in GS:[0] */
    movq %gs:8, %rsp      /* Load kernel RSP from GS:[8] */
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %rbx
    pushq %rax
    /* RCX = RIP, R11 = RFLAGS (syscall convention) */
    pushq %rcx            /* RIP */
    pushq %r11            /* RFLAGS */
    /* Call handler */
    call syscall_handler
    /* Restore */
    popq %r11             /* RFLAGS */
    popq %rcx             /* RIP */
    popq %rax
    popq %rbx
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rbp
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    movq %gs:0, %rsp      /* Restore user RSP */
    swapgs
    sysretq
.size syscall_stub, . - syscall_stub

/* IRQ Stubs (0-15) */
IRQ_STUB 0   /* Timer */
IRQ_STUB 1   /* Keyboard */
IRQ_STUB 2   /* Cascade */
IRQ_STUB 3   /* COM2 */
IRQ_STUB 4   /* COM1 */
IRQ_STUB 5   /* LPT2 */
IRQ_STUB 6   /* Floppy */
IRQ_STUB 7   /* LPT1 */
IRQ_STUB 8   /* RTC */
IRQ_STUB 9   /* ACPI */
IRQ_STUB 10  /* Available */
IRQ_STUB 11  /* Available */
IRQ_STUB 12  /* PS/2 Mouse */
IRQ_STUB 13  /* FPU/Coprocessor */
IRQ_STUB 14  /* Primary ATA */
IRQ_STUB 15  /* Secondary ATA */

/* Array of ISR stub pointers */
.section .rodata
.global isr_stubs
.align 8
isr_stubs:
    .quad isr_stub_0
    .quad isr_stub_1
    .quad isr_stub_2
    .quad isr_stub_3
    .quad isr_stub_4
    .quad isr_stub_5
    .quad isr_stub_6
    .quad isr_stub_7
    .quad isr_stub_8
    .quad isr_stub_9
    .quad isr_stub_10
    .quad isr_stub_11
    .quad isr_stub_12
    .quad isr_stub_13
    .quad isr_stub_14
    .quad isr_stub_15
    .quad isr_stub_16
    .quad isr_stub_17
    .quad isr_stub_18
    .quad isr_stub_19
    .quad isr_stub_20
    .quad isr_stub_21
    .quad isr_stub_22
    .quad isr_stub_23
    .quad isr_stub_24
    .quad isr_stub_25
    .quad isr_stub_26
    .quad isr_stub_27
    .quad isr_stub_28
    .quad isr_stub_29
    .quad isr_stub_30
    .quad isr_stub_31
    .quad syscall_stub  /* Index 32 for syscall */

/* Array of IRQ stub pointers */
.global irq_stubs
.align 8
irq_stubs:
    .quad irq_stub_0
    .quad irq_stub_1
    .quad irq_stub_2
    .quad irq_stub_3
    .quad irq_stub_4
    .quad irq_stub_5
    .quad irq_stub_6
    .quad irq_stub_7
    .quad irq_stub_8
    .quad irq_stub_9
    .quad irq_stub_10
    .quad irq_stub_11
    .quad irq_stub_12
    .quad irq_stub_13
    .quad irq_stub_14
    .quad irq_stub_15

/* Common ISR handler (called from stubs) */
.section .text
.global isr_common
.type isr_common, @function
isr_common:
    /* Stack layout:
     * [rsp]     = vector number
     * [rsp+8]   = error code (or dummy)
     * [rsp+16]  = RIP
     * [rsp+24]  = CS
     * [rsp+32]  = RFLAGS
     * [rsp+40]  = RSP (user)
     * [rsp+48]  = SS
     */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    
    /* Pass pointer to exception_frame to handler */
    movq %rsp, %rdi
    call isr_common_handler
    
    /* Restore */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    
    /* Remove vector and error code */
    addq $16, %rsp
    iretq
.size isr_common, . - isr_common

/* Common IRQ handler */
.global irq_common
.type irq_common, @function
irq_common:
    /* Stack layout:
     * [rsp]     = IRQ number (0-15)
     * [rsp+8]   = dummy error code
     * [rsp+16]  = RIP
     * [rsp+24]  = CS
     * [rsp+32]  = RFLAGS
     * [rsp+40]  = RSP (user)
     * [rsp+48]  = SS
     */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    popq %r14
    popq %r15
    
    /* Pass pointer to interrupt_frame to handler */
    /* IRQ number is at rsp + 8*16 (after pushes) + 8 (dummy) = rsp + 136 */
    movq %rsp, %rdi
    call irq_common_handler
    
    /* Restore */
    pushq %r14
    pushq %r15
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    
    /* Remove IRQ number and dummy error code */
    addq $16, %rsp
    iretq
.size irq_common, . - irq_common