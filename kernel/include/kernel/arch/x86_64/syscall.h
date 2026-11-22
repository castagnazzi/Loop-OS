/* SYSCALL/SYSRET support */

#ifndef _KERNEL_ARCH_X86_64_SYSCALL_H
#define _KERNEL_ARCH_X86_64_SYSCALL_H

#include <stdint.h>

/* Program the MSRs (EFER.SCE, STAR, LSTAR, SFMASK). Needs the GDT loaded. */
void syscall_init(void);

/* Kernel stack top used by syscall_entry; switched together with the task */
extern uint64_t syscall_kernel_rsp;

#endif
