/* Intel e1000 (82540EM and relatives) Ethernet driver: polled, no interrupts. */

#include <kernel/net/nic.h>
#include <kernel/pci.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vmm.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_EERD   0x0014
#define REG_ICR    0x00C0
#define REG_IMC    0x00D8
#define REG_RCTL   0x0100
#define REG_TCTL   0x0400
#define REG_TIPG   0x0410
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_MTA    0x5200
#define REG_RAL0   0x5400
#define REG_RAH0   0x5404

#define RX_COUNT 32
#define TX_COUNT 8
#define BUF_SIZE 2048

struct rx_desc { uint64_t addr; uint16_t length, csum; uint8_t status, errors; uint16_t special; } __attribute__((packed));
struct tx_desc { uint64_t addr; uint16_t length; uint8_t cso, cmd, status, css; uint16_t special; } __attribute__((packed));

static volatile uint8_t *mmio = NULL;
static struct rx_desc *rx_ring;
static struct tx_desc *tx_ring;
static uint8_t *rx_bufs, *tx_bufs;
static int rx_cur = 0, tx_cur = 0;

static uint32_t rd(uint32_t reg) { return *(volatile uint32_t *)(mmio + reg); }
static void wr(uint32_t reg, uint32_t v) { *(volatile uint32_t *)(mmio + reg) = v; }

static void spin(int n) { for (volatile int i = 0; i < n; i++) {} }

static const uint16_t supported[] = {
    0x100E, 0x100F, 0x1004, 0x1008, 0x1009, 0x100C, 0x1010, 0x1015, 0x1016, 0x1017, 0x10D3, 0x153A, 0x1539,
};

static uint16_t eeprom_word(int addr) {
    wr(REG_EERD, ((uint32_t)addr << 8) | 1);
    for (int i = 0; i < 100000; i++) {
        uint32_t v = rd(REG_EERD);
        if (v & 0x10) return (uint16_t)(v >> 16);
    }
    return 0;
}

static int e1000_link_up(void);

static int e1000_init(uint8_t mac[6]) {
    const struct pci_dev *nic = NULL;
    for (int i = 0; i < pci_count() && !nic; i++) {
        const struct pci_dev *d = pci_get(i);
        if (d->vendor != 0x8086) continue;
        for (unsigned k = 0; k < sizeof(supported) / sizeof(supported[0]); k++) {
            if (d->device == supported[k]) { nic = d; break; }
        }
    }
    if (!nic) return -1;

    pci_enable(nic);
    uint32_t bar0 = pci_read32(nic, 0x10);
    if (bar0 & 1) return -1;                                   /* I/O BAR: not supported */
    uint64_t phys = bar0 & ~0xFULL;

    /* Map the 128 KB register window uncached */
    mmio = pci_map_mmio(phys, 32);
    if (!mmio) return -1;

    /* Reset, no interrupts (we poll) */
    wr(REG_IMC, 0xFFFFFFFF);
    wr(REG_CTRL, rd(REG_CTRL) | (1u << 26));
    spin(2000000);
    for (int i = 0; i < 1000 && (rd(REG_CTRL) & (1u << 26)); i++) spin(10000);
    wr(REG_IMC, 0xFFFFFFFF);
    rd(REG_ICR);

    /* Link up */
    uint32_t ctrl = rd(REG_CTRL);
    ctrl |= (1u << 6) | (1u << 5);                              /* SLU, ASDE */
    ctrl &= ~((1u << 3) | (1u << 31) | (1u << 30));             /* LRST, PHY_RST, VME */
    wr(REG_CTRL, ctrl);

    /* MAC address */
    uint32_t ral = rd(REG_RAL0), rah = rd(REG_RAH0);
    if (ral == 0 && (rah & 0xFFFF) == 0) {
        uint16_t w0 = eeprom_word(0), w1 = eeprom_word(1), w2 = eeprom_word(2);
        mac[0] = (uint8_t)w0; mac[1] = (uint8_t)(w0 >> 8);
        mac[2] = (uint8_t)w1; mac[3] = (uint8_t)(w1 >> 8);
        mac[4] = (uint8_t)w2; mac[5] = (uint8_t)(w2 >> 8);
        wr(REG_RAL0, (uint32_t)mac[0] | (mac[1] << 8) | (mac[2] << 16) | ((uint32_t)mac[3] << 24));
        wr(REG_RAH0, (uint32_t)mac[4] | (mac[5] << 8) | (1u << 31));
    } else {
        mac[0] = (uint8_t)ral; mac[1] = (uint8_t)(ral >> 8); mac[2] = (uint8_t)(ral >> 16); mac[3] = (uint8_t)(ral >> 24);
        mac[4] = (uint8_t)rah; mac[5] = (uint8_t)(rah >> 8);
    }
    for (int i = 0; i < 128; i++) wr(REG_MTA + i * 4, 0);

    /* DMA memory comes from the physical allocator and is reached through the HHDM */
    uint64_t hhdm = vmm_get_hhdm();
    void *ring_phys = pmm_alloc(1);
    void *rxb_phys = pmm_alloc(RX_COUNT * BUF_SIZE / 4096);
    void *txb_phys = pmm_alloc(TX_COUNT * BUF_SIZE / 4096);
    void *txr_phys = pmm_alloc(1);
    if (!ring_phys || !rxb_phys || !txb_phys || !txr_phys) return -1;

    rx_ring = (struct rx_desc *)((uint64_t)ring_phys + hhdm);
    tx_ring = (struct tx_desc *)((uint64_t)txr_phys + hhdm);
    rx_bufs = (uint8_t *)((uint64_t)rxb_phys + hhdm);
    tx_bufs = (uint8_t *)((uint64_t)txb_phys + hhdm);
    memset(rx_ring, 0, 4096);
    memset(tx_ring, 0, 4096);

    for (int i = 0; i < RX_COUNT; i++) rx_ring[i].addr = (uint64_t)rxb_phys + (uint64_t)i * BUF_SIZE;
    for (int i = 0; i < TX_COUNT; i++) {
        tx_ring[i].addr = (uint64_t)txb_phys + (uint64_t)i * BUF_SIZE;
        tx_ring[i].status = 1;                                  /* DD: free */
    }

    wr(REG_RDBAL, (uint32_t)(uint64_t)ring_phys);
    wr(REG_RDBAH, (uint32_t)((uint64_t)ring_phys >> 32));
    wr(REG_RDLEN, RX_COUNT * 16);
    wr(REG_RDH, 0);
    wr(REG_RDT, RX_COUNT - 1);
    wr(REG_RCTL, (1u << 1) | (1u << 15) | (1u << 26));          /* EN, BAM, SECRC; 2048-byte buffers */

    wr(REG_TDBAL, (uint32_t)(uint64_t)txr_phys);
    wr(REG_TDBAH, (uint32_t)((uint64_t)txr_phys >> 32));
    wr(REG_TDLEN, TX_COUNT * 16);
    wr(REG_TDH, 0);
    wr(REG_TDT, 0);
    wr(REG_TIPG, 0x0060200A);
    wr(REG_TCTL, (1u << 1) | (1u << 3) | (0x10u << 4) | (0x40u << 12));   /* EN, PSP, CT, COLD */

    kprintf("e1000: %02x:%02x:%02x:%02x:%02x:%02x, link %s\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
            e1000_link_up() ? "up" : "down");
    return 0;
}

static int e1000_link_up(void) {
    return mmio && (rd(REG_STATUS) & 2);
}

static int e1000_send(const void *frame, size_t len) {
    if (!mmio || len == 0 || len > 1514) return -1;
    struct tx_desc *d = &tx_ring[tx_cur];
    for (int i = 0; i < 1000000 && !(d->status & 1); i++) {}
    if (!(d->status & 1)) return -1;
    memcpy(tx_bufs + (size_t)tx_cur * BUF_SIZE, frame, len);
    if (len < 60) {
        memset(tx_bufs + (size_t)tx_cur * BUF_SIZE + len, 0, 60 - len);   /* pad to the Ethernet minimum */
        len = 60;
    }
    d->length = (uint16_t)len;
    d->cmd = 0x0B;                                              /* EOP | IFCS | RS */
    d->status = 0;
    tx_cur = (tx_cur + 1) % TX_COUNT;
    asm volatile("" ::: "memory");
    wr(REG_TDT, (uint32_t)tx_cur);
    return 0;
}

static void e1000_poll(void (*handler)(const uint8_t *frame, size_t len)) {
    if (!mmio) return;
    for (;;) {
        struct rx_desc *d = &rx_ring[rx_cur];
        if (!(d->status & 1)) break;                            /* DD not set: empty */
        if ((d->status & 2) && !d->errors && d->length > 0) {
            handler(rx_bufs + (size_t)rx_cur * BUF_SIZE, d->length);
        }
        d->status = 0;
        wr(REG_RDT, (uint32_t)rx_cur);
        rx_cur = (rx_cur + 1) % RX_COUNT;
    }
}

const struct nic_driver e1000_driver = { "e1000", e1000_init, e1000_send, e1000_poll, e1000_link_up };
