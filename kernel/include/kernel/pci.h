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

/* Allow memory-space access and DMA for the device */
void pci_enable(const struct pci_dev *d);

#endif
