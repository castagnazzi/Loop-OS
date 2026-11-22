/* IDE/ATA disk driver (PIO mode, polling).
 *
 * Probes the four legacy positions (primary/secondary, master/slave) and
 * reads with LBA28, or LBA48 for sectors beyond 128 GiB. ATAPI devices
 * (CD-ROM) are skipped. Interrupts from the controller stay masked. */

#include <kernel/blk.h>
#include <kernel/mm/heap.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

#define R_DATA   0
#define R_COUNT  2
#define R_LBA0   3
#define R_LBA1   4
#define R_LBA2   5
#define R_DRIVE  6
#define R_STATUS 7
#define R_CMD    7

#define ST_ERR 0x01
#define ST_DRQ 0x08
#define ST_DF  0x20
#define ST_BSY 0x80

struct ata_dev {
    uint16_t io, ctrl;
    int slave;
    int lba48;
};

static inline void outb(uint16_t p, uint8_t v) { asm volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t r; asm volatile("inb %1, %0" : "=a"(r) : "Nd"(p)); return r; }
static inline void insw(uint16_t p, void *buf, int words) {
    asm volatile("rep insw" : "+D"(buf), "+c"(words) : "d"(p) : "memory");
}

static void delay400(struct ata_dev *d) {
    for (int i = 0; i < 4; i++) inb(d->ctrl);
}

static int wait_ready(struct ata_dev *d) {
    for (int i = 0; i < 2000000; i++) {
        if (!(inb(d->io + R_STATUS) & ST_BSY)) return 0;
    }
    return -1;
}

static int wait_drq(struct ata_dev *d) {
    for (int i = 0; i < 2000000; i++) {
        uint8_t s = inb(d->io + R_STATUS);
        if (s & (ST_ERR | ST_DF)) return -1;
        if (!(s & ST_BSY) && (s & ST_DRQ)) return 0;
    }
    return -1;
}

static int ata_read(struct blkdev *bd, uint64_t lba, uint32_t count, void *buf) {
    struct ata_dev *d = bd->priv;
    uint8_t *out = buf;
    if (lba + count > bd->sectors) return -1;

    while (count) {
        uint32_t n = count > 128 ? 128 : count;
        if (wait_ready(d)) return -1;

        if (d->lba48 || lba + n > 0x0FFFFFFF) {
            if (!d->lba48) return -1;
            outb(d->io + R_DRIVE, (uint8_t)(0x40 | (d->slave << 4)));
            outb(d->io + R_COUNT, (uint8_t)(n >> 8));
            outb(d->io + R_LBA0, (uint8_t)(lba >> 24));
            outb(d->io + R_LBA1, (uint8_t)(lba >> 32));
            outb(d->io + R_LBA2, (uint8_t)(lba >> 40));
            outb(d->io + R_COUNT, (uint8_t)n);
            outb(d->io + R_LBA0, (uint8_t)lba);
            outb(d->io + R_LBA1, (uint8_t)(lba >> 8));
            outb(d->io + R_LBA2, (uint8_t)(lba >> 16));
            outb(d->io + R_CMD, 0x24);              /* READ SECTORS EXT */
        } else {
            outb(d->io + R_DRIVE, (uint8_t)(0xE0 | (d->slave << 4) | ((lba >> 24) & 0x0F)));
            outb(d->io + R_COUNT, (uint8_t)n);
            outb(d->io + R_LBA0, (uint8_t)lba);
            outb(d->io + R_LBA1, (uint8_t)(lba >> 8));
            outb(d->io + R_LBA2, (uint8_t)(lba >> 16));
            outb(d->io + R_CMD, 0x20);              /* READ SECTORS */
        }
        delay400(d);

        for (uint32_t i = 0; i < n; i++) {
            if (wait_drq(d)) return -1;
            insw(d->io + R_DATA, out, 256);
            out += BLK_SECTOR;
        }
        lba += n;
        count -= n;
    }
    return 0;
}

static void probe(uint16_t io, uint16_t ctrl, int slave, int index) {
    struct ata_dev tmp = { io, ctrl, slave, 0 };

    outb(ctrl, 0x02);                               /* nIEN: no interrupts */
    outb(io + R_DRIVE, (uint8_t)(0xA0 | (slave << 4)));
    delay400(&tmp);
    outb(io + R_COUNT, 0);
    outb(io + R_LBA0, 0);
    outb(io + R_LBA1, 0);
    outb(io + R_LBA2, 0);
    outb(io + R_CMD, 0xEC);                         /* IDENTIFY */

    uint8_t st = inb(io + R_STATUS);
    if (st == 0 || st == 0xFF) return;              /* nothing there */
    if (wait_ready(&tmp)) return;
    if (inb(io + R_LBA1) || inb(io + R_LBA2)) return;   /* ATAPI or SATA signature */
    if (wait_drq(&tmp)) return;

    uint16_t id[256];
    insw(io + R_DATA, id, 256);

    struct blkdev proto;
    memset(&proto, 0, sizeof(proto));
    proto.sectors = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    int lba48 = (id[83] >> 10) & 1;
    if (lba48) {
        uint64_t s48 = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                       ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
        if (s48) proto.sectors = s48;
    }
    if (proto.sectors == 0) return;

    /* Model string: 20 words, bytes swapped inside each word */
    for (int i = 0; i < 20; i++) {
        proto.model[i * 2] = (char)(id[27 + i] >> 8);
        proto.model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    proto.model[40] = '\0';
    for (int i = 39; i >= 0 && proto.model[i] == ' '; i--) proto.model[i] = '\0';

    struct ata_dev *dev = kmalloc(sizeof(*dev));
    if (!dev) return;
    *dev = tmp;
    dev->lba48 = lba48;

    proto.name[0] = 'h'; proto.name[1] = 'd'; proto.name[2] = (char)('a' + index);
    proto.read = ata_read;
    proto.priv = dev;
    if (blk_add(&proto)) {
        kprintf("ata: %s: %s, %lu MB\n", proto.name, proto.model,
                (unsigned long)(proto.sectors / 2048));
    }
}

void ata_init(void) {
    probe(0x1F0, 0x3F6, 0, 0);
    probe(0x1F0, 0x3F6, 1, 1);
    probe(0x170, 0x376, 0, 2);
    probe(0x170, 0x376, 1, 3);
}
