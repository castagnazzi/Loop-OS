/* SYSCALL/SYSRET setup (MSRs) */

#include <kernel/arch/x86_64/syscall.h>
#include <kernel/arch/x86_64/gdt.h>
#include <kernel/stdio.h>
#include <stdint.h>

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_SFMASK 0xC0000084

#define EFER_SCE (1ULL << 0)

extern void syscall_entry(void);

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    asm volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

void syscall_init(void) {
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);

    /* STAR[47:32]: kernel CS for SYSCALL (SS = +8).
     * STAR[63:48]: base for SYSRET: SS = base+8, CS = base+16, both |3.
     * With user data at 0x18 and user code at 0x20 the base is 0x10. */
    wrmsr(MSR_STAR, ((uint64_t)0x10 << 48) | ((uint64_t)GDT_KERNEL_CODE << 32));
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* Clear IF, TF and DF on entry: the kernel runs with interrupts off */
    wrmsr(MSR_SFMASK, 0x200 | 0x100 | 0x400);

    kprintf("Syscall: SYSCALL/SYSRET enabled\n");
}
