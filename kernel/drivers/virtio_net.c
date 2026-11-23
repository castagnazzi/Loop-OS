/* virtio-net driver (legacy/transitional PCI interface through I/O ports, polled). */

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

#define V_GUEST_FEATURES 4
#define V_QUEUE_ADDR     8
#define V_QUEUE_SIZE     12
#define V_QUEUE_SEL      14
#define V_QUEUE_NOTIFY   16
#define V_STATUS         18
#define V_CONFIG         20

#define HDR_LEN  10                 /* virtio_net_hdr without mergeable buffers */
#define BUF_SIZE 2048
#define NBUF     32

struct vq {
    uint16_t size;
    uint8_t *mem;                   /* virtual */
    uint64_t phys;
    struct { uint64_t addr; uint32_t len; uint16_t flags, next; } *desc;
    volatile uint16_t *avail_idx, *avail_ring;
    volatile uint16_t *used_idx;
    volatile struct { uint32_t id, len; } *used_ring;
    uint16_t last_used;
};

static uint16_t io = 0;
static struct vq rxq, txq;
static uint8_t *rx_bufs, *tx_buf;
static uint64_t rx_phys, tx_phys;

static int vq_setup(struct vq *q, int index) {
    outw(io + V_QUEUE_SEL, (uint16_t)index);
    q->size = inw(io + V_QUEUE_SIZE);
    if (q->size == 0 || q->size > 1024) return -1;
    size_t desc_bytes = 16u * q->size;
    size_t avail_bytes = 6u + 2u * q->size;
    size_t used_off = (desc_bytes + avail_bytes + 4095) & ~4095u;
    size_t total = used_off + 6u + 8u * q->size;
    size_t pages = (total + 4095) / 4096;
    q->phys = (uint64_t)pmm_alloc_dma(pages);
    if (!q->phys) return -1;
    q->mem = (uint8_t *)(q->phys + vmm_get_hhdm());
    memset(q->mem, 0, pages * 4096);
    q->desc = (void *)q->mem;
    q->avail_idx = (volatile uint16_t *)(q->mem + desc_bytes + 2);
    q->avail_ring = (volatile uint16_t *)(q->mem + desc_bytes + 4);
    q->used_idx = (volatile uint16_t *)(q->mem + used_off + 2);
    q->used_ring = (void *)(q->mem + used_off + 4);
    q->last_used = 0;
    outl(io + V_QUEUE_ADDR, (uint32_t)(q->phys >> 12));
    return 0;
}

static void post_rx(int id) {
    rxq.desc[id].addr = rx_phys + (uint64_t)id * BUF_SIZE;
    rxq.desc[id].len = BUF_SIZE;
    rxq.desc[id].flags = 2;                       /* device writes */
    rxq.avail_ring[*rxq.avail_idx % rxq.size] = (uint16_t)id;
    asm volatile("" ::: "memory");
    (*rxq.avail_idx)++;
}

static int virtio_init(uint8_t mac[6]) {
    const struct pci_dev *d = pci_find(0x1AF4, 0x1000);
    if (!d) return -1;
    int is_io;
    uint64_t bar = pci_bar(d, 0, &is_io);
    if (!bar || !is_io) return -1;
    pci_enable(d);
    io = (uint16_t)bar;

    outb(io + V_STATUS, 0);                       /* reset */
    outb(io + V_STATUS, 1 | 2);                   /* acknowledge + driver */
    outl(io + V_GUEST_FEATURES, 0);               /* no optional features */
    for (int i = 0; i < 6; i++) mac[i] = inb(io + V_CONFIG + i);

    if (vq_setup(&rxq, 0) != 0 || vq_setup(&txq, 1) != 0) return -1;

    uint64_t hhdm = vmm_get_hhdm();
    rx_phys = (uint64_t)pmm_alloc_dma(NBUF * BUF_SIZE / 4096);
    tx_phys = (uint64_t)pmm_alloc_dma(1);
    if (!rx_phys || !tx_phys) return -1;
    rx_bufs = (uint8_t *)(rx_phys + hhdm);
    tx_buf = (uint8_t *)(tx_phys + hhdm);

    for (int i = 0; i < NBUF && i < rxq.size; i++) post_rx(i);
    outb(io + V_STATUS, 1 | 2 | 4);               /* driver ok */
    outw(io + V_QUEUE_NOTIFY, 0);
    kprintf("virtio-net: %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}

static int virtio_link_up(void) { return io != 0; }

static int virtio_send(const void *frame, size_t len) {
    if (!io || len == 0 || len > 1514) return -1;
    memset(tx_buf, 0, HDR_LEN);
    memcpy(tx_buf + HDR_LEN, frame, len);
    if (len < 60) { memset(tx_buf + HDR_LEN + len, 0, 60 - len); len = 60; }
    txq.desc[0].addr = tx_phys;
    txq.desc[0].len = (uint32_t)(HDR_LEN + len);
    txq.desc[0].flags = 0;
    txq.avail_ring[*txq.avail_idx % txq.size] = 0;
    asm volatile("" ::: "memory");
    (*txq.avail_idx)++;
    outw(io + V_QUEUE_NOTIFY, 1);
    /* one buffer: wait until the device has consumed it */
    for (int i = 0; i < 2000000; i++) {
        if (*txq.used_idx != txq.last_used) { txq.last_used = *txq.used_idx; return 0; }
    }
    return -1;
}

static void virtio_poll(void (*handler)(const uint8_t *frame, size_t len)) {
    if (!io) return;
    int posted = 0;
    while (*rxq.used_idx != rxq.last_used) {
        asm volatile("" ::: "memory");
        uint32_t id = rxq.used_ring[rxq.last_used % rxq.size].id;
        uint32_t len = rxq.used_ring[rxq.last_used % rxq.size].len;
        rxq.last_used++;
        if (id < NBUF && len > HDR_LEN) handler(rx_bufs + (size_t)id * BUF_SIZE + HDR_LEN, len - HDR_LEN);
        post_rx((int)id);
        posted = 1;
    }
    if (posted) outw(io + V_QUEUE_NOTIFY, 0);
}

const struct nic_driver virtio_net_driver = { "virtio-net", virtio_init, virtio_send, virtio_poll, virtio_link_up };
