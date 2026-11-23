/* Realtek RTL8139 Ethernet driver (I/O ports, polled). Common in old PCs and in QEMU (-device rtl8139). */

#include <kernel/net/nic.h>
#include <kernel/pci.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vmm.h>
#include <kernel/string.h>
#include <kernel/stdio.h>
#include <stdint.h>

static inline void outb(uint16_t p, uint8_t v) { asm volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v) { asm volatile("outw %0, %1" : : "a"(v), "Nd"(p)); }
static inline void outl(uint16_t p, uint32_t v) { asm volatile("outl %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t r; asm volatile("inb %1, %0" : "=a"(r) : "Nd"(p)); return r; }
static inline uint16_t inw(uint16_t p) { uint16_t r; asm volatile("inw %1, %0" : "=a"(r) : "Nd"(p)); return r; }
static inline uint32_t inl(uint16_t p) { uint32_t r; asm volatile("inl %1, %0" : "=a"(r) : "Nd"(p)); return r; }

#define R_IDR0    0x00
#define R_TSD0    0x10
#define R_TSAD0   0x20
#define R_RBSTART 0x30
#define R_CR      0x37
#define R_CAPR    0x38
#define R_IMR     0x3C
#define R_TCR     0x40
#define R_RCR     0x44
#define R_CONFIG1 0x52
#define R_MSR     0x58

#define RX_LEN   8192
#define TX_BUF   2048

static uint16_t io = 0;
static uint8_t *rx_buf, *tx_buf;
static uint64_t rx_phys, tx_phys;
static uint32_t rx_off = 0;
static int tx_cur = 0, tx_used[4];

static int rtl8139_init(uint8_t mac[6]) {
    const struct pci_dev *d = pci_find(0x10EC, 0x8139);
    if (!d) return -1;
    int is_io;
    uint64_t bar = pci_bar(d, 0, &is_io);
    if (!bar || !is_io) return -1;
    pci_enable(d);
    io = (uint16_t)bar;

    outb(io + R_CONFIG1, 0);                       /* power on */
    outb(io + R_CR, 0x10);                         /* reset */
    for (int i = 0; i < 100000 && (inb(io + R_CR) & 0x10); i++) {}

    uint64_t hhdm = vmm_get_hhdm();
    rx_phys = (uint64_t)pmm_alloc_dma(5);          /* 8 KB ring + room for one wrapped frame */
    tx_phys = (uint64_t)pmm_alloc_dma(2);
    if (!rx_phys || !tx_phys) return -1;
    rx_buf = (uint8_t *)(rx_phys + hhdm);
    tx_buf = (uint8_t *)(tx_phys + hhdm);
    memset(rx_buf, 0, 5 * 4096);

    for (int i = 0; i < 6; i++) mac[i] = inb(io + R_IDR0 + i);

    outl(io + R_RBSTART, (uint32_t)rx_phys);
    outw(io + R_IMR, 0);                           /* polled */
    outl(io + R_RCR, 0x0F | (1u << 7));            /* accept all kinds; WRAP keeps frames contiguous */
    outb(io + R_CR, 0x0C);                         /* RX + TX enable */
    outl(io + R_TCR, 3u << 24);
    rx_off = 0;
    tx_cur = 0;
    for (int i = 0; i < 4; i++) tx_used[i] = 0;
    kprintf("rtl8139: %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}

static int rtl8139_link_up(void) {
    return io && !(inb(io + R_MSR) & 4);
}

static int rtl8139_send(const void *frame, size_t len) {
    if (!io || len == 0 || len > 1514) return -1;
    /* A descriptor in use is free again once the chip sets OWN (bit 13) */
    if (tx_used[tx_cur]) {
        int ok = 0;
        for (int i = 0; i < 1000000; i++) {
            if (inl(io + R_TSD0 + tx_cur * 4) & (1u << 13)) { ok = 1; break; }
        }
        if (!ok) return -1;
    }
    uint8_t *buf = tx_buf + tx_cur * TX_BUF;
    memcpy(buf, frame, len);
    if (len < 60) { memset(buf + len, 0, 60 - len); len = 60; }
    outl(io + R_TSAD0 + tx_cur * 4, (uint32_t)(tx_phys + (uint64_t)tx_cur * TX_BUF));
    outl(io + R_TSD0 + tx_cur * 4, (uint32_t)len);   /* writing the length starts the send */
    tx_used[tx_cur] = 1;
    tx_cur = (tx_cur + 1) & 3;
    return 0;
}

static void rtl8139_poll(void (*handler)(const uint8_t *frame, size_t len)) {
    if (!io) return;
    while (!(inb(io + R_CR) & 1)) {                  /* BUFE: buffer empty */
        uint8_t *hdr = rx_buf + rx_off;
        uint16_t status = (uint16_t)(hdr[0] | (hdr[1] << 8));
        uint16_t len = (uint16_t)(hdr[2] | (hdr[3] << 8));
        if (len < 4 || len > 1600 + 4 || !(status & 1)) {
            /* corrupted: reset the receiver rather than loop forever */
            outb(io + R_CR, 0x04);
            outb(io + R_CR, 0x0C);
            outl(io + R_RBSTART, (uint32_t)rx_phys);
            rx_off = 0;
            return;
        }
        handler(hdr + 4, (size_t)len - 4);
        rx_off = (rx_off + len + 4 + 3) & ~3u;
        rx_off %= RX_LEN;
        outw(io + R_CAPR, (uint16_t)(rx_off - 16));
    }
}

const struct nic_driver rtl8139_driver = { "rtl8139", rtl8139_init, rtl8139_send, rtl8139_poll, rtl8139_link_up };
