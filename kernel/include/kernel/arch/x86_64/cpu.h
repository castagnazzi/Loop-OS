/* CPU Detection and Features */

#ifndef _KERNEL_ARCH_X86_64_CPU_H
#define _KERNEL_ARCH_X86_64_CPU_H

#include <stdint.h>

/* CPUID feature flags */
struct cpu_features {
    /* Standard features (eax=1) */
    uint32_t fpu       : 1;  /* x87 FPU */
    uint32_t vme       : 1;  /* Virtual 8086 Mode Enhancements */
    uint32_t de        : 1;  /* Debugging Extensions */
    uint32_t pse       : 1;  /* Page Size Extensions */
    uint32_t tsc       : 1;  /* Time Stamp Counter */
    uint32_t msr       : 1;  /* Model Specific Registers */
    uint32_t pae       : 1;  /* Physical Address Extension */
    uint32_t mce       : 1;  /* Machine Check Exception */
    uint32_t cx8       : 1;  /* CMPXCHG8B */
    uint32_t apic      : 1;  /* APIC */
    uint32_t reserved1 : 1;
    uint32_t sep       : 1;  /* SYSENTER/SYSEXIT */
    uint32_t mtrr      : 1;  /* Memory Type Range Registers */
    uint32_t pge       : 1;  /* Page Global Enable */
    uint32_t mca       : 1;  /* Machine Check Architecture */
    uint32_t cmov      : 1;  /* Conditional Move */
    uint32_t pat       : 1;  /* Page Attribute Table */
    uint32_t pse36     : 1;  /* 36-bit Page Size Extension */
    uint32_t psn       : 1;  /* Processor Serial Number */
    uint32_t clfsh     : 1;  /* CLFLUSH */
    uint32_t reserved2 : 1;
    uint32_t ds        : 1;  /* Debug Store */
    uint32_t acpi      : 1;  /* Thermal Monitor */
    uint32_t mmx       : 1;  /* MMX */
    uint32_t fxsr      : 1;  /* FXSAVE/FXRSTOR */
    uint32_t sse       : 1;  /* SSE */
    uint32_t sse2      : 1;  /* SSE2 */
    uint32_t ss        : 1;  /* Self Snoop */
    uint32_t htt       : 1;  /* Hyper-Threading */
    uint32_t tm        : 1;  /* Thermal Monitor */
    uint32_t reserved3 : 1;
    uint32_t pbe       : 1;  /* Pending Break Enable */

    /* Extended features (eax=7, ecx=0) */
    uint32_t fsgsbase  : 1;
    uint32_t tsc_adjust: 1;
    uint32_t bmi1      : 1;
    uint32_t hle       : 1;
    uint32_t avx2      : 1;
    uint32_t smep      : 1;
    uint32_t bmi2      : 1;
    uint32_t erms      : 1;
    uint32_t invpcid   : 1;
    uint32_t rtm       : 1;
    uint32_t avx512f   : 1;

    /* XCR0 features (XSAVE) */
    uint32_t xsave     : 1;
    uint32_t avx       : 1;
    uint32_t osxsave   : 1;
};

/* CPU vendor string (12 chars + null) */
extern char cpu_vendor[13];

/* CPU brand string (48 chars + null) */
extern char cpu_brand[49];

/* Detected features */
extern struct cpu_features cpu_features;

/* Initialize CPU detection */
void cpu_detect(void);

/* Check if feature is supported */
int cpu_has_feature(const char *name);

/* Read MSR */
uint64_t cpu_read_msr(uint32_t msr);

/* Write MSR */
void cpu_write_msr(uint32_t msr, uint64_t value);

#endif