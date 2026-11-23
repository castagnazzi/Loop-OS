/* PCI bus enumeration (configuration mechanism #1, I/O ports) */

#ifndef _KERNEL_PCI_H
#define _KERNEL_PCI_H

#include <stdint.h>

struct pci_dev {
    uint8_t bus, dev, fn;
    uint16_t vendor, device;
    uint8_t class_code, subclass, prog_if;
    uint8_t irq_line;
};

#define PCI_MAX_DEVICES 64

/* Scan the bus and remember what is there */
void pci_init(void);
int pci_count(void);
const struct pci_dev *pci_get(int index);

uint32_t pci_read32(const struct pci_dev *d, uint8_t off);
void pci_write32(const struct pci_dev *d, uint8_t off, uint32_t value);
uint16_t pci_read16(const struct pci_dev *d, uint8_t off);
void pci_write16(const struct pci_dev *d, uint8_t off, uint16_t value);

/* Allow I/O and memory-space access and DMA for the device */
void pci_enable(const struct pci_dev *d);

/* Base address register 'idx' (0-5): returns the base, sets *is_io. 0 if unused. */
uint64_t pci_bar(const struct pci_dev *d, int idx, int *is_io);

/* Map 'pages' pages of device memory (uncached) and return the virtual address, or NULL */
void *pci_map_mmio(uint64_t phys, int pages);

/* First device with this id, or NULL */
const struct pci_dev *pci_find(uint16_t vendor, uint16_t device);

#endif
