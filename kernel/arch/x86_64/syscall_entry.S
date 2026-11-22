/* SYSCALL entry point and kernel context switch
 *
 * SYSCALL (from ring 3) does not touch the stack: RCX = user RIP,
 * R11 = user RFLAGS, RSP still points into user memory, and the CPU masks
 * the flags in SFMASK (we clear IF, so the kernel runs without interrupts).
 *
 * The kernel is single CPU, so the stack to switch to lives in a global
 * (syscall_kernel_rsp, updated on every task switch) instead of per-CPU
 * data behind SWAPGS.
 *
 * Frame built on the kernel stack (lowest address first), matching
 * struct syscall_frame in syscalls.c:
 *   r15 r14 r13 r12 rbp rbx r9 r8 r10 rdx rsi rdi rax rcx(rip) r11(rflags) user_rsp
 */

.section .text
.code64

.global syscall_entry
.type syscall_entry, @function
.align 16
syscall_entry:
    movq %rsp, syscall_user_rsp(%rip)
    movq syscall_kernel_rsp(%rip), %rsp

    pushq syscall_user_rsp(%rip)
    pushq %r11
    pushq %rcx
    pushq %rax
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %r10
    pushq %r8
    pushq %r9
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi
    call syscall_dispatch

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    popq %r9
    popq %r8
    popq %r10
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rax
    popq %rcx
    popq %r11
    popq %rsp               /* back on the user stack */
    sysretq
.size syscall_entry, . - syscall_entry

/* void switch_to(uint64_t *old_rsp_save, uint64_t new_rsp)
 *
 * Saves the callee-saved registers on the current kernel stack, stores RSP
 * in *old_rsp_save, loads new_rsp and restores the other task's registers.
 * The final 'ret' resumes the new task where it last called switch_to (or,
 * for a fresh task, in user_entry_stub below). */
.global switch_to
.type switch_to, @function
.align 16
switch_to:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)
    movq %rsi, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size switch_to, . - switch_to

/* First code run by a new user task: the kernel stack was prepared so that
 * RSP points at an iretq frame (rip, cs, rflags, rsp, ss). Clear the
 * general registers so no kernel values leak into user mode. */
.global user_entry_stub
.type user_entry_stub, @function
.align 16
user_entry_stub:
    xorl %eax, %eax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %esi, %esi
    xorl %edi, %edi
    xorl %ebp, %ebp
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    xorl %r10d, %r10d
    xorl %r11d, %r11d
    xorl %r12d, %r12d
    xorl %r13d, %r13d
    xorl %r14d, %r14d
    xorl %r15d, %r15d
    iretq
.size user_entry_stub, . - user_entry_stub

.section .bss
.align 8
.global syscall_kernel_rsp
syscall_kernel_rsp:
    .skip 8
.global syscall_user_rsp
syscall_user_rsp:
    .skip 8
