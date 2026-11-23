/* Realtek RTL8168/8169 (and RTL8111) Gigabit Ethernet driver, memory-mapped, polled.
 * These are the cards on most PC motherboards. Not tested on real hardware: QEMU does not emulate them. */

#include <kernel/net/nic.h>
#include <kernel/pci.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vmm.h>
#include <kernel/string.h>
#include <kernel/stdio.h>
#include <stdint.h>

#define R_IDR0   0x00
#define R_TNPDS  0x20
#define R_CR     0x37
#define R_TPPOLL 0x38
#define R_IMR    0x3C
#define R_ISR    0x3E
#define R_TCR    0x40
#define R_RCR    0x44
#define R_9346CR 0x50
#define R_PHYST  0x6C
#define R_RMS    0xDA
#define R_CPCR   0xE0
#define R_RDSAR  0xE4
#define R_MTPS   0xEC

#define DESC_OWN (1u << 31)
#define DESC_EOR (1u << 30)
#define DESC_FS  (1u << 29)
#define DESC_LS  (1u << 28)

#define NRX 32
#define NTX 8
#define BUF_SIZE 2048

struct desc { volatile uint32_t opts1, opts2; volatile uint64_t addr; } __attribute__((packed));

static volatile uint8_t *regs = NULL;
static struct desc *rx_ring, *tx_ring;
static uint8_t *rx_bufs, *tx_bufs;
static uint64_t rx_ring_phys, tx_ring_phys, rx_bufs_phys, tx_bufs_phys;
static int rx_cur = 0, tx_cur = 0;

static uint8_t r8(int r) { return *(volatile uint8_t *)(regs + r); }
static uint16_t r16(int r) { return *(volatile uint16_t *)(regs + r); }
static uint32_t r32(int r) { return *(volatile uint32_t *)(regs + r); }
static void w8(int r, uint8_t v) { *(volatile uint8_t *)(regs + r) = v; }
static void w16(int r, uint16_t v) { *(volatile uint16_t *)(regs + r) = v; }
static void w32(int r, uint32_t v) { *(volatile uint32_t *)(regs + r) = v; }

static const uint16_t ids[] = { 0x8168, 0x8169, 0x8167, 0x8161 };

static int rtl8169_init(uint8_t mac[6]) {
    const struct pci_dev *d = NULL;
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]) && !d; i++) d = pci_find(0x10EC, ids[i]);
    if (!d) return -1;

    /* the registers are in the first memory BAR (BAR2 on the 8168, BAR1 on the 8169) */
    uint64_t phys = 0;
    for (int b = 0; b < 6 && !phys; b++) {
        int is_io;
        uint64_t v = pci_bar(d, b, &is_io);
        if (v && !is_io) phys = v;
        if (!is_io && v && (pci_read32(d, (uint8_t)(0x10 + b * 4)) & 6) == 4) b++;   /* skip the upper half */
    }
    if (!phys) return -1;
    pci_enable(d);
    regs = pci_map_mmio(phys, 1);
    if (!regs) return -1;

    w8(R_CR, 0x10);                                  /* reset */
    for (int i = 0; i < 100000 && (r8(R_CR) & 0x10); i++) {}
    w16(R_IMR, 0);                                   /* polled */
    w16(R_ISR, 0xFFFF);

    for (int i = 0; i < 6; i++) mac[i] = r8(R_IDR0 + i);

    uint64_t hhdm = vmm_get_hhdm();
    rx_ring_phys = (uint64_t)pmm_alloc_dma(1);
    tx_ring_phys = (uint64_t)pmm_alloc_dma(1);
    rx_bufs_phys = (uint64_t)pmm_alloc_dma(NRX * BUF_SIZE / 4096);
    tx_bufs_phys = (uint64_t)pmm_alloc_dma(NTX * BUF_SIZE / 4096);
    if (!rx_ring_phys || !tx_ring_phys || !rx_bufs_phys || !tx_bufs_phys) return -1;
    rx_ring = (struct desc *)(rx_ring_phys + hhdm);
    tx_ring = (struct desc *)(tx_ring_phys + hhdm);
    rx_bufs = (uint8_t *)(rx_bufs_phys + hhdm);
    tx_bufs = (uint8_t *)(tx_bufs_phys + hhdm);
    memset(rx_ring, 0, 4096);
    memset(tx_ring, 0, 4096);
    for (int i = 0; i < NRX; i++) {
        rx_ring[i].addr = rx_bufs_phys + (uint64_t)i * BUF_SIZE;
        rx_ring[i].opts1 = DESC_OWN | BUF_SIZE | (i == NRX - 1 ? DESC_EOR : 0);
    }
    for (int i = 0; i < NTX; i++) {
        tx_ring[i].addr = tx_bufs_phys + (uint64_t)i * BUF_SIZE;
        tx_ring[i].opts1 = (i == NTX - 1 ? DESC_EOR : 0);
    }

    w8(R_9346CR, 0xC0);                              /* unlock the configuration registers */
    w16(R_CPCR, r16(R_CPCR) & ~0x0040);              /* no VLAN/checksum offload */
    w16(R_RMS, 0x1FFF);
    w8(R_MTPS, 0x3B);
    w32(R_TNPDS, (uint32_t)tx_ring_phys);
    w32(R_TNPDS + 4, (uint32_t)(tx_ring_phys >> 32));
    w32(R_RDSAR, (uint32_t)rx_ring_phys);
    w32(R_RDSAR + 4, (uint32_t)(rx_ring_phys >> 32));
    w8(R_CR, 0x0C);                                  /* RX + TX enable */
    w32(R_TCR, (7u << 8) | (3u << 24));              /* unlimited DMA bursts, normal IFG */
    w32(R_RCR, 0x0F | (7u << 8) | (7u << 13));       /* all kinds of frames, no FIFO threshold */
    w8(R_9346CR, 0x00);
    rx_cur = tx_cur = 0;
    kprintf("rtl8169: %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}

static int rtl8169_link_up(void) {
    return regs && (r8(R_PHYST) & 2);
}

static int rtl8169_send(const void *frame, size_t len) {
    if (!regs || len == 0 || len > 1514) return -1;
    struct desc *t = &tx_ring[tx_cur];
    for (int i = 0; i < 1000000 && (t->opts1 & DESC_OWN); i++) {}
    if (t->opts1 & DESC_OWN) return -1;
    uint8_t *buf = tx_bufs + (size_t)tx_cur * BUF_SIZE;
    memcpy(buf, frame, len);
    if (len < 60) { memset(buf + len, 0, 60 - len); len = 60; }
    t->opts2 = 0;
    asm volatile("" ::: "memory");
    t->opts1 = DESC_OWN | DESC_FS | DESC_LS | (uint32_t)len | (tx_cur == NTX - 1 ? DESC_EOR : 0);
    tx_cur = (tx_cur + 1) % NTX;
    w8(R_TPPOLL, 0x40);                              /* kick the normal-priority queue */
    return 0;
}

static void rtl8169_poll(void (*handler)(const uint8_t *frame, size_t len)) {
    if (!regs) return;
    for (;;) {
        struct desc *d = &rx_ring[rx_cur];
        uint32_t o = d->opts1;
        if (o & DESC_OWN) break;
        uint32_t len = o & 0x3FFF;
        if (!(o & (1u << 21)) && (o & DESC_FS) && (o & DESC_LS) && len > 4)
            handler(rx_bufs + (size_t)rx_cur * BUF_SIZE, len - 4);
        d->opts1 = DESC_OWN | BUF_SIZE | (rx_cur == NRX - 1 ? DESC_EOR : 0);
        rx_cur = (rx_cur + 1) % NRX;
    }
}

const struct nic_driver rtl8169_driver = { "rtl8169", rtl8169_init, rtl8169_send, rtl8169_poll, rtl8169_link_up };
