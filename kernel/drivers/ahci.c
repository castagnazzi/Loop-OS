/* AHCI SATA disk driver (polled DMA, one command at a time).
 *
 * Almost every PC made after ~2010 runs its SATA disks in AHCI mode, where the legacy
 * IDE ports used by ata.c do not exist. Read-only, like the filesystem drivers above it.
 * Disks appear as sda, sdb, ... NVMe drives are not supported yet. */

#include <kernel/blk.h>
#include <kernel/pci.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vmm.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

#define HBA_GHC   0x04
#define HBA_PI    0x0C
#define P_BASE    0x100
#define P_SIZE    0x80

#define P_CLB   0x00
#define P_CLBU  0x04
#define P_FB    0x08
#define P_FBU   0x0C
#define P_IS    0x10
#define P_CMD   0x18
#define P_TFD   0x20
#define P_SIG   0x24
#define P_SSTS  0x28
#define P_SERR  0x30
#define P_CI    0x38

#define CMD_ST  (1u << 0)
#define CMD_FRE (1u << 4)
#define CMD_FR  (1u << 14)
#define CMD_CR  (1u << 15)

#define BOUNCE_SECTORS 128

struct ahci_port {
    volatile uint8_t *regs;
    uint8_t *page;              /* command list, FIS area and command table (virtual) */
    uint64_t page_phys;
    uint8_t *bounce;
    uint64_t bounce_phys;
};

static uint32_t rd(struct ahci_port *p, int r) { return *(volatile uint32_t *)(p->regs + r); }
static void wr(struct ahci_port *p, int r, uint32_t v) { *(volatile uint32_t *)(p->regs + r) = v; }

static int port_stop(struct ahci_port *p) {
    wr(p, P_CMD, rd(p, P_CMD) & ~(CMD_ST | CMD_FRE));
    for (int i = 0; i < 1000000; i++) {
        if (!(rd(p, P_CMD) & (CMD_CR | CMD_FR))) return 0;
    }
    return -1;
}

static void port_start(struct ahci_port *p) {
    for (int i = 0; i < 1000000 && (rd(p, P_CMD) & CMD_CR); i++) {}
    wr(p, P_CMD, rd(p, P_CMD) | CMD_FRE);
    wr(p, P_CMD, rd(p, P_CMD) | CMD_ST);
}

/* Run one command with a single data buffer (bytes > 0) from the bounce area. 0 on success. */
static int run(struct ahci_port *p, uint8_t command, uint64_t lba, uint32_t count, uint32_t bytes) {
    uint32_t *hdr = (uint32_t *)p->page;                       /* command header 0 */
    uint8_t *ct = p->page + 0x500;                             /* command table */
    memset(ct, 0, 0x100);

    hdr[0] = 5 | (1u << 16);                                   /* CFL = 5 dwords, PRDTL = 1 */
    hdr[1] = 0;                                                /* PRDBC */
    hdr[2] = (uint32_t)(p->page_phys + 0x500);
    hdr[3] = (uint32_t)((p->page_phys + 0x500) >> 32);

    ct[0] = 0x27;                                              /* register host-to-device FIS */
    ct[1] = 0x80;                                              /* command */
    ct[2] = command;
    ct[7] = 0x40;                                              /* LBA mode */
    ct[4] = (uint8_t)lba; ct[5] = (uint8_t)(lba >> 8); ct[6] = (uint8_t)(lba >> 16);
    ct[8] = (uint8_t)(lba >> 24); ct[9] = (uint8_t)(lba >> 32); ct[10] = (uint8_t)(lba >> 40);
    ct[12] = (uint8_t)count; ct[13] = (uint8_t)(count >> 8);

    uint8_t *prd = ct + 0x80;                                  /* PRDT entry 0 */
    uint64_t b = p->bounce_phys;
    prd[0] = (uint8_t)b; prd[1] = (uint8_t)(b >> 8); prd[2] = (uint8_t)(b >> 16); prd[3] = (uint8_t)(b >> 24);
    prd[4] = (uint8_t)(b >> 32); prd[5] = (uint8_t)(b >> 40); prd[6] = (uint8_t)(b >> 48); prd[7] = (uint8_t)(b >> 56);
    uint32_t dbc = bytes - 1;
    prd[12] = (uint8_t)dbc; prd[13] = (uint8_t)(dbc >> 8); prd[14] = (uint8_t)(dbc >> 16); prd[15] = (uint8_t)(dbc >> 24);

    wr(p, P_IS, 0xFFFFFFFF);
    for (int i = 0; i < 1000000 && (rd(p, P_TFD) & 0x88); i++) {}   /* BSY | DRQ */
    asm volatile("" ::: "memory");
    wr(p, P_CI, 1);
    for (int i = 0; i < 20000000; i++) {
        if (!(rd(p, P_CI) & 1)) return (rd(p, P_TFD) & 0x01) || (rd(p, P_IS) & (1u << 30)) ? -1 : 0;
        if (rd(p, P_IS) & (1u << 30)) return -1;                    /* task file error */
    }
    return -1;
}

static int ahci_read(struct blkdev *bd, uint64_t lba, uint32_t count, void *buf) {
    struct ahci_port *p = bd->priv;
    uint8_t *out = buf;
    if (lba + count > bd->sectors) return -1;
    while (count) {
        uint32_t n = count > BOUNCE_SECTORS ? BOUNCE_SECTORS : count;
        if (run(p, 0x25, lba, n, n * BLK_SECTOR) != 0) return -1;   /* READ DMA EXT */
        memcpy(out, p->bounce, (size_t)n * BLK_SECTOR);
        out += (size_t)n * BLK_SECTOR;
        lba += n;
        count -= n;
    }
    return 0;
}

static void probe_port(volatile uint8_t *hba, int n, int *index) {
    struct ahci_port tmp;
    tmp.regs = hba + P_BASE + n * P_SIZE;
    struct ahci_port *p = &tmp;

    uint32_t ssts = rd(p, P_SSTS);
    if ((ssts & 0xF) != 3 || ((ssts >> 8) & 0xF) != 1) return;     /* no device / link down */
    if (rd(p, P_SIG) != 0x00000101) return;                        /* not a plain ATA disk (e.g. ATAPI) */

    if (port_stop(p) != 0) return;
    uint64_t hhdm = vmm_get_hhdm();
    uint64_t page = (uint64_t)pmm_alloc_dma(1);
    uint64_t bounce = (uint64_t)pmm_alloc_dma(BOUNCE_SECTORS * BLK_SECTOR / 4096);
    if (!page || !bounce) return;
    tmp.page = (uint8_t *)(page + hhdm);
    tmp.page_phys = page;
    tmp.bounce = (uint8_t *)(bounce + hhdm);
    tmp.bounce_phys = bounce;
    memset(tmp.page, 0, 4096);

    wr(p, P_CLB, (uint32_t)page);
    wr(p, P_CLBU, (uint32_t)(page >> 32));
    wr(p, P_FB, (uint32_t)(page + 0x400));
    wr(p, P_FBU, (uint32_t)((page + 0x400) >> 32));
    wr(p, P_SERR, 0xFFFFFFFF);
    wr(p, P_IS, 0xFFFFFFFF);
    port_start(p);

    if (run(p, 0xEC, 0, 0, 512) != 0) return;                      /* IDENTIFY */
    uint16_t *id = (uint16_t *)tmp.bounce;

    struct blkdev proto;
    memset(&proto, 0, sizeof(proto));
    proto.sectors = (uint64_t)id[100] | ((uint64_t)id[101] << 16) | ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
    if (!proto.sectors) proto.sectors = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    if (!proto.sectors) return;
    for (int i = 0; i < 20; i++) {
        proto.model[i * 2] = (char)(id[27 + i] >> 8);
        proto.model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    proto.model[40] = '\0';
    for (int i = 39; i >= 0 && proto.model[i] == ' '; i--) proto.model[i] = '\0';

    struct ahci_port *dev = kmalloc(sizeof(*dev));
    if (!dev) return;
    *dev = tmp;
    proto.name[0] = 's'; proto.name[1] = 'd'; proto.name[2] = (char)('a' + *index);
    proto.read = ahci_read;
    proto.priv = dev;
    if (blk_add(&proto)) {
        (*index)++;
        kprintf("ahci: %s: %s, %lu MB\n", proto.name, proto.model, (unsigned long)(proto.sectors / 2048));
    }
}

void ahci_init(void) {
    pci_init();                                                    /* the bus scan is needed before the network starts */
    const struct pci_dev *d = NULL;
    for (int i = 0; i < pci_count(); i++) {
        const struct pci_dev *c = pci_get(i);
        if (c->class_code == 0x01 && c->subclass == 0x06 && c->prog_if == 0x01) { d = c; break; }
    }
    if (!d) return;

    int is_io;
    uint64_t abar = pci_bar(d, 5, &is_io);
    if (!abar || is_io) return;
    pci_enable(d);
    volatile uint8_t *hba = pci_map_mmio(abar, 3);
    if (!hba) return;

    *(volatile uint32_t *)(hba + HBA_GHC) |= 1u << 31;             /* AHCI enable */
    uint32_t pi = *(volatile uint32_t *)(hba + HBA_PI);
    int index = 0;
    for (int n = 0; n < 32; n++) {
        if (pi & (1u << n)) probe_port(hba, n, &index);
    }
}
