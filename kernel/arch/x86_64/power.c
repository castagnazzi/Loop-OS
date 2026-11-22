/* Power control: shutdown and reboot.
 *
 * Shutdown follows ACPI: find the FADT through the RSDP, read the sleep type
 * for state S5 out of the DSDT (_S5 object) and write SLP_TYP | SLP_EN to the
 * PM1 control registers. Emulator-specific ports are tried afterwards.
 * Reboot uses the ACPI reset register when present, then the keyboard
 * controller, then a deliberate triple fault. */

#include <kernel/arch/x86_64/power.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/limine.h>
#include <kernel/mm/vmm.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

extern volatile struct limine_rsdp_request limine_rsdp_request;

static inline void outb(uint16_t p, uint8_t v) { asm volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v) { asm volatile("outw %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint16_t inw(uint16_t p) { uint16_t r; asm volatile("inw %1, %0" : "=a"(r) : "Nd"(p)); return r; }

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t rd64(const uint8_t *p) { return rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

/* Physical memory -> pointer. ACPI tables in reclaimable memory are already in
 * the HHDM; the RSDP (BIOS ROM) and tables in reserved areas are not, so those
 * pages are mapped on demand into a private window. */
static uint64_t win_next = VMM_MODULES_BASE;

static const uint8_t *acpi_ptr(uint64_t addr, uint64_t len) {
    uint64_t first = addr & ~0xFFFULL, last = (addr + len - 1) & ~0xFFFULL;
    uint64_t hhdm = vmm_get_hhdm();

    int in_hhdm = 1;
    for (uint64_t pg = first; pg <= last; pg += VMM_PAGE_SIZE) {
        if (!vmm_virt_to_phys((void *)(pg + hhdm))) { in_hhdm = 0; break; }
    }
    if (in_hhdm) return (const uint8_t *)(addr + hhdm);

    uint64_t va = win_next;
    for (uint64_t pg = first; pg <= last; pg += VMM_PAGE_SIZE) {
        if (vmm_map((void *)win_next, (void *)pg, 1, VMM_FLAG_PRESENT | VMM_FLAG_NX) != 0) return NULL;
        win_next += VMM_PAGE_SIZE;
    }
    return (const uint8_t *)(va + (addr & 0xFFF));
}

/* A whole table: map the header first to learn its length */
static const uint8_t *acpi_table(uint64_t addr) {
    const uint8_t *h = acpi_ptr(addr, 36);
    if (!h) return NULL;
    uint32_t len = rd32(h + 4);
    if (len < 36 || len > 4 * 1024 * 1024) return NULL;
    return acpi_ptr(addr, len);
}

static const uint8_t *rsdp_ptr(void) {
    if (!limine_rsdp_request.response) return NULL;
    uint64_t a = (uint64_t)limine_rsdp_request.response->address;
    if (a == 0) return NULL;
    /* Newer Limine revisions hand out a physical address, older ones a virtual one */
    if (a >= vmm_get_hhdm()) a -= vmm_get_hhdm();
    return acpi_ptr(a, 36);
}

static const uint8_t *find_table(const char *sig) {
    const uint8_t *rsdp = rsdp_ptr();
    if (!rsdp || memcmp(rsdp, "RSD PTR ", 8) != 0) return NULL;

    int xsdt = rsdp[15] >= 2 && rd64(rsdp + 24) != 0;
    const uint8_t *root = acpi_table(xsdt ? rd64(rsdp + 24) : rd32(rsdp + 16));
    if (!root) return NULL;
    uint32_t len = rd32(root + 4);
    uint32_t entry = xsdt ? 8 : 4;
    for (uint32_t off = 36; off + entry <= len; off += entry) {
        uint64_t addr = xsdt ? rd64(root + off) : rd32(root + off);
        const uint8_t *t = acpi_ptr(addr, 36);
        if (t && memcmp(t, sig, 4) == 0) return acpi_table(addr);
    }
    return NULL;
}

/* SLP_TYP values from the \_S5 package in the DSDT; returns 0 on success */
static int read_s5(const uint8_t *dsdt, uint8_t *typa, uint8_t *typb) {
    if (!dsdt) return -1;
    uint32_t len = rd32(dsdt + 4);
    for (uint32_t i = 36; i + 8 < len; i++) {
        if (memcmp(dsdt + i, "_S5_", 4) != 0) continue;
        if (i < 1 || (dsdt[i - 1] != 0x08 && !(i >= 2 && dsdt[i - 1] == '\\' && dsdt[i - 2] == 0x08))) continue;
        const uint8_t *q = dsdt + i + 4;
        if (*q != 0x12) continue;                 /* PackageOp */
        q++;
        q += ((*q & 0xC0) >> 6) + 1;              /* PkgLength */
        q++;                                      /* NumElements */
        if (*q == 0x0A) q++;                      /* BytePrefix */
        *typa = *q++;
        if (*q == 0x0A) q++;
        *typb = *q;
        return 0;
    }
    return -1;
}

void power_off(void) {
    interrupts_save();                            /* no interrupts from here on */
    kprintf("Shutting down...\n");

    const uint8_t *fadt = find_table("FACP");
    if (fadt) {
        uint32_t smi = rd32(fadt + 48);
        uint8_t acpi_enable = fadt[52];
        uint16_t pm1a = (uint16_t)rd32(fadt + 64), pm1b = (uint16_t)rd32(fadt + 68);
        uint64_t dsdt_addr = rd32(fadt + 40);
        if (rd32(fadt + 4) >= 148 && rd64(fadt + 140)) dsdt_addr = rd64(fadt + 140);

        uint8_t typa = 0, typb = 0;
        if (dsdt_addr && pm1a && read_s5(acpi_table(dsdt_addr), &typa, &typb) == 0) {
            /* Make sure ACPI mode is on */
            if (smi && !(inw(pm1a) & 1)) {
                outb((uint16_t)smi, acpi_enable);
                for (int i = 0; i < 300 && !(inw(pm1a) & 1); i++) {
                    for (volatile int d = 0; d < 100000; d++) {}
                }
            }
            outw(pm1a, (uint16_t)((typa << 10) | 0x2000));
            if (pm1b) outw(pm1b, (uint16_t)((typb << 10) | 0x2000));
            for (volatile int d = 0; d < 20000000; d++) {}
        }
    }

    /* Emulators that do not describe it (or old ones) */
    outw(0x604, 0x2000);       /* QEMU (newer machine types) */
    outw(0xB004, 0x2000);      /* Bochs / old QEMU */
    outw(0x4004, 0x3400);      /* VirtualBox */
    outw(0x600, 0x34);         /* Cloud Hypervisor */

    kprintf("It is now safe to turn off the computer.\n");
    for (;;) asm volatile("cli; hlt");
}

void power_reboot(void) {
    interrupts_save();
    kprintf("Rebooting...\n");

    /* ACPI reset register (I/O space only) */
    const uint8_t *fadt = find_table("FACP");
    if (fadt && rd32(fadt + 4) >= 129 && (rd32(fadt + 112) & (1u << 10)) && fadt[116] == 1) {
        outb((uint16_t)rd64(fadt + 120), fadt[128]);
        for (volatile int d = 0; d < 5000000; d++) {}
    }

    /* Keyboard controller reset line */
    outb(0x64, 0xFE);
    for (volatile int d = 0; d < 5000000; d++) {}

    /* Last resort: load an empty IDT and fault */
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) empty = { 0, 0 };
    asm volatile("lidt %0; int3" : : "m"(empty));
    for (;;) asm volatile("cli; hlt");
}
