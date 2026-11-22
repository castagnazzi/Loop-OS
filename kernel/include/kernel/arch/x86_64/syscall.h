/* SYSCALL/SYSRET support */

#ifndef _KERNEL_ARCH_X86_64_SYSCALL_H
#define _KERNEL_ARCH_X86_64_SYSCALL_H

#include <stdint.h>

/* Program the MSRs (EFER.SCE, STAR, LSTAR, SFMASK). Needs the GDT loaded. */
void syscall_init(void);

/* Matches the frame pushed by syscall_entry (lowest address first) */
struct syscall_frame {
    uint64_t r15, r14, r13, r12, rbp, rbx, r9, r8, r10, rdx, rsi, rdi;
    uint64_t rax;
    uint64_t rip;        /* RCX */
    uint64_t rflags;     /* R11 */
    uint64_t user_rsp;
};

/* Kernel stack top used by syscall_entry; switched together with the task */
extern uint64_t syscall_kernel_rsp;

#endif
