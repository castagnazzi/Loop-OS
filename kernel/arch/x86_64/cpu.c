/* CPU Detection Implementation */

#include <kernel/arch/x86_64/cpu.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

char cpu_vendor[13] = {0};
char cpu_brand[49] = {0};
struct cpu_features cpu_features = {0};

static inline void cpuid(uint32_t eax, uint32_t ecx, uint32_t *out_eax, uint32_t *out_ebx, uint32_t *out_ecx, uint32_t *out_edx) {
    asm volatile("cpuid"
        : "=a"(*out_eax), "=b"(*out_ebx), "=c"(*out_ecx), "=d"(*out_edx)
        : "a"(eax), "c"(ecx)
    );
}

static inline uint64_t xgetbv(uint32_t xcr) {
    uint32_t eax, edx;
    asm volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(xcr));
    return ((uint64_t)edx << 32) | eax;
}

void cpu_detect(void) {
    uint32_t eax, ebx, ecx, edx;
    uint32_t max_standard, max_extended;

    /* Get vendor string */
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    max_standard = eax;
    *(uint32_t*)(cpu_vendor + 0) = ebx;
    *(uint32_t*)(cpu_vendor + 4) = edx;
    *(uint32_t*)(cpu_vendor + 8) = ecx;
    cpu_vendor[12] = '\0';

    kprintf("CPU: Vendor: %s\n", cpu_vendor);

    /* Standard features (eax=1) */
    if (max_standard >= 1) {
        cpuid(1, 0, &eax, &ebx, &ecx, &edx);
        
        cpu_features.fpu       = (edx >> 0)  & 1;
        cpu_features.vme       = (edx >> 1)  & 1;
        cpu_features.de        = (edx >> 2)  & 1;
        cpu_features.pse       = (edx >> 3)  & 1;
        cpu_features.tsc       = (edx >> 4)  & 1;
        cpu_features.msr       = (edx >> 5)  & 1;
        cpu_features.pae       = (edx >> 6)  & 1;
        cpu_features.mce       = (edx >> 7)  & 1;
        cpu_features.cx8       = (edx >> 8)  & 1;
        cpu_features.apic      = (edx >> 9)  & 1;
        cpu_features.sep       = (edx >> 11) & 1;
        cpu_features.mtrr      = (edx >> 12) & 1;
        cpu_features.pge       = (edx >> 13) & 1;
        cpu_features.mca       = (edx >> 14) & 1;
        cpu_features.cmov      = (edx >> 15) & 1;
        cpu_features.pat       = (edx >> 16) & 1;
        cpu_features.pse36     = (edx >> 17) & 1;
        cpu_features.psn       = (edx >> 18) & 1;
        cpu_features.clfsh     = (edx >> 19) & 1;
        cpu_features.ds        = (edx >> 21) & 1;
        cpu_features.acpi      = (edx >> 22) & 1;
        cpu_features.mmx       = (edx >> 23) & 1;
        cpu_features.fxsr      = (edx >> 24) & 1;
        cpu_features.sse       = (edx >> 25) & 1;
        cpu_features.sse2      = (edx >> 26) & 1;
        cpu_features.ss        = (edx >> 27) & 1;
        cpu_features.htt       = (edx >> 28) & 1;
        cpu_features.tm        = (edx >> 29) & 1;
        cpu_features.pbe       = (edx >> 31) & 1;

        cpu_features.xsave     = (ecx >> 26) & 1;
        cpu_features.osxsave   = (ecx >> 27) & 1;
        cpu_features.avx       = (ecx >> 28) & 1;
    }

    /* Extended features (eax=7) */
    if (max_standard >= 7) {
        cpuid(7, 0, &eax, &ebx, &ecx, &edx);
        cpu_features.fsgsbase  = (ebx >> 0)  & 1;
        cpu_features.tsc_adjust= (ebx >> 1)  & 1;
        cpu_features.bmi1      = (ebx >> 3)  & 1;
        cpu_features.hle       = (ebx >> 4)  & 1;
        cpu_features.avx2      = (ebx >> 5)  & 1;
        cpu_features.smep      = (ebx >> 7)  & 1;
        cpu_features.bmi2      = (ebx >> 8)  & 1;
        cpu_features.erms      = (ebx >> 9)  & 1;
        cpu_features.invpcid   = (ebx >> 10) & 1;
        cpu_features.rtm       = (ebx >> 11) & 1;
        cpu_features.avx512f   = (ebx >> 16) & 1;
    }

    /* Extended function info */
    cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    max_extended = eax;

    /* Brand string */
    if (max_extended >= 0x80000004) {
        cpuid(0x80000002, 0, &eax, &ebx, &ecx, &edx);
        *(uint32_t*)(cpu_brand + 0)  = eax;
        *(uint32_t*)(cpu_brand + 4)  = ebx;
        *(uint32_t*)(cpu_brand + 8)  = ecx;
        *(uint32_t*)(cpu_brand + 12) = edx;
        cpuid(0x80000003, 0, &eax, &ebx, &ecx, &edx);
        *(uint32_t*)(cpu_brand + 16) = eax;
        *(uint32_t*)(cpu_brand + 20) = ebx;
        *(uint32_t*)(cpu_brand + 24) = ecx;
        *(uint32_t*)(cpu_brand + 28) = edx;
        cpuid(0x80000004, 0, &eax, &ebx, &ecx, &edx);
        *(uint32_t*)(cpu_brand + 32) = eax;
        *(uint32_t*)(cpu_brand + 36) = ebx;
        *(uint32_t*)(cpu_brand + 40) = ecx;
        *(uint32_t*)(cpu_brand + 44) = edx;
        cpu_brand[48] = '\0';
        kprintf("CPU: Brand: %s\n", cpu_brand);
    }

    /* Check XSAVE features */
    if (cpu_features.osxsave) {
        uint64_t xcr0 = xgetbv(0);
        cpu_features.avx = (xcr0 & 0x6) == 0x6;  /* XMM + YMM state */
    }

    kprintf("CPU: Features: SSE=%d SSE2=%d AVX=%d AVX2=%d XSAVE=%d\n",
            cpu_features.sse, cpu_features.sse2, cpu_features.avx, cpu_features.avx2, cpu_features.xsave);
}

int cpu_has_feature(const char *name) {
    if (!name) return 0;
    /* Simple string matching for common features */
    if (strcmp(name, "sse") == 0) return cpu_features.sse;
    if (strcmp(name, "sse2") == 0) return cpu_features.sse2;
    if (strcmp(name, "avx") == 0) return cpu_features.avx;
    if (strcmp(name, "avx2") == 0) return cpu_features.avx2;
    if (strcmp(name, "xsave") == 0) return cpu_features.xsave;
    if (strcmp(name, "fsgsbase") == 0) return cpu_features.fsgsbase;
    if (strcmp(name, "smep") == 0) return cpu_features.smep;
    if (strcmp(name, "invpcid") == 0) return cpu_features.invpcid;
    return 0;
}

uint64_t cpu_read_msr(uint32_t msr) {
    uint32_t eax, edx;
    asm volatile("rdmsr" : "=a"(eax), "=d"(edx) : "c"(msr));
    return ((uint64_t)edx << 32) | eax;
}

void cpu_write_msr(uint32_t msr, uint64_t value) {
    asm volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

/* strcmp from string.h */
extern int strcmp(const char *s1, const char *s2);