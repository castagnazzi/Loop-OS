/* PCI enumeration */

#include <kernel/pci.h>
#include <kernel/stdio.h>
#include <stdint.h>

static inline void outl(uint16_t p, uint32_t v) { asm volatile("outl %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint32_t inl(uint16_t p) { uint32_t r; asm volatile("inl %1, %0" : "=a"(r) : "Nd"(p)); return r; }

static struct pci_dev devs[PCI_MAX_DEVICES];
static int ndevs = 0;

static uint32_t cfg_read(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC));
    return inl(0xCFC);
}

static void cfg_write(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v) {
    outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC));
    outl(0xCFC, v);
}

uint32_t pci_read32(const struct pci_dev *d, uint8_t off) { return cfg_read(d->bus, d->dev, d->fn, off); }
void pci_write32(const struct pci_dev *d, uint8_t off, uint32_t v) { cfg_write(d->bus, d->dev, d->fn, off, v); }

uint16_t pci_read16(const struct pci_dev *d, uint8_t off) {
    return (uint16_t)(cfg_read(d->bus, d->dev, d->fn, off) >> ((off & 2) * 8));
}

void pci_write16(const struct pci_dev *d, uint8_t off, uint16_t v) {
    uint32_t w = cfg_read(d->bus, d->dev, d->fn, off);
    int sh = (off & 2) * 8;
    w = (w & ~(0xFFFFu << sh)) | ((uint32_t)v << sh);
    cfg_write(d->bus, d->dev, d->fn, off, w);
}

void pci_enable(const struct pci_dev *d) {
    uint16_t cmd = pci_read16(d, 0x04);
    cmd |= 0x0006;                 /* memory space + bus master */
    cmd &= (uint16_t)~0x0400;      /* interrupts not disabled */
    pci_write16(d, 0x04, cmd);
}

static void scan_function(uint8_t bus, uint8_t dev, uint8_t fn) {
    uint32_t id = cfg_read(bus, dev, fn, 0);
    if ((id & 0xFFFF) == 0xFFFF || ndevs >= PCI_MAX_DEVICES) return;
    struct pci_dev *d = &devs[ndevs++];
    d->bus = bus; d->dev = dev; d->fn = fn;
    d->vendor = (uint16_t)id;
    d->device = (uint16_t)(id >> 16);
    uint32_t cls = cfg_read(bus, dev, fn, 8);
    d->class_code = (uint8_t)(cls >> 24);
    d->subclass = (uint8_t)(cls >> 16);
    d->prog_if = (uint8_t)(cls >> 8);
    d->irq_line = (uint8_t)cfg_read(bus, dev, fn, 0x3C);
}

void pci_init(void) {
    ndevs = 0;
    for (int bus = 0; bus < 8; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            uint32_t id = cfg_read((uint8_t)bus, (uint8_t)dev, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF) continue;
            int multi = (cfg_read((uint8_t)bus, (uint8_t)dev, 0, 0x0C) >> 16) & 0x80;
            for (int fn = 0; fn < (multi ? 8 : 1); fn++) scan_function((uint8_t)bus, (uint8_t)dev, (uint8_t)fn);
        }
    }
    for (int i = 0; i < ndevs; i++) {
        kprintf("pci: %02x:%02x.%x %04x:%04x class %02x%02x\n", devs[i].bus, devs[i].dev, devs[i].fn,
                devs[i].vendor, devs[i].device, devs[i].class_code, devs[i].subclass);
    }
}

int pci_count(void) { return ndevs; }
const struct pci_dev *pci_get(int i) { return i >= 0 && i < ndevs ? &devs[i] : NULL; }
