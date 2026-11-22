/* Block device table and partition tables (MBR and GPT) */

#include <kernel/blk.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

static struct blkdev devs[BLK_MAX];
static int ndevs = 0;

struct blkdev *blk_add(const struct blkdev *proto) {
    if (ndevs >= BLK_MAX) return NULL;
    devs[ndevs] = *proto;
    return &devs[ndevs++];
}

int blk_count(void) { return ndevs; }
struct blkdev *blk_get(int i) { return i >= 0 && i < ndevs ? &devs[i] : NULL; }

int blk_read(struct blkdev *dev, uint64_t lba, uint32_t count, void *buf) {
    if (lba + count > dev->sectors) return -1;
    if (dev->parent) return blk_read(dev->parent, dev->offset + lba, count, buf);
    return dev->read(dev, lba, count, buf);
}

static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t le64(const uint8_t *p) { return le32(p) | ((uint64_t)le32(p + 4) << 32); }

static const char *mbr_kind(uint8_t t) {
    switch (t) {
    case 0x01: return "FAT12";
    case 0x04: case 0x06: case 0x0E: return "FAT16";
    case 0x0B: case 0x0C: return "FAT32";
    case 0x07: return "NTFS/exFAT";
    case 0x83: return "Linux";
    case 0x82: return "Linux swap";
    case 0xEF: return "EFI";
    case 0x05: case 0x0F: return "Extended";
    default: return "other";
    }
}

static int is_guid(const uint8_t *g, uint32_t a, uint16_t b, uint16_t c) {
    return le32(g) == a && g[4] == (b & 0xFF) && g[5] == (b >> 8) && g[6] == (c & 0xFF) && g[7] == (c >> 8);
}

static const char *gpt_kind(const uint8_t *g) {
    if (is_guid(g, 0xEBD0A0A2, 0xB9E5, 0x4433)) return "Basic data";     /* FAT32 / NTFS */
    if (is_guid(g, 0x0FC63DAF, 0x8483, 0x4772)) return "Linux";
    if (is_guid(g, 0xC12A7328, 0xF81F, 0x11D2)) return "EFI";
    if (is_guid(g, 0xE3C9E316, 0x0B5C, 0x4DB8)) return "MS reserved";
    if (is_guid(g, 0x0657FD6D, 0xA4AB, 0x43C4)) return "Linux swap";
    return "other";
}

static void add_part(struct blkdev *disk, int number, uint64_t first, uint64_t count, const char *kind) {
    if (count == 0 || first + count > disk->sectors) return;
    struct blkdev p;
    memset(&p, 0, sizeof(p));
    size_t n = strlen(disk->name);
    memcpy(p.name, disk->name, n);
    p.name[n] = (char)('0' + number);
    p.sectors = count;
    p.parent = disk;
    p.offset = first;
    strncpy(p.kind, kind, sizeof(p.kind) - 1);
    if (blk_add(&p)) {
        kprintf("part: %s: %lu MB, %s\n", p.name, (unsigned long)(count / 2048), kind);
    }
}

void part_scan(struct blkdev *disk) {
    uint8_t sec[BLK_SECTOR];
    if (blk_read(disk, 0, 1, sec) != 0) return;
    if (sec[510] != 0x55 || sec[511] != 0xAA) return;

    /* Protective MBR: the real table is the GPT */
    if (sec[450] == 0xEE) {
        uint8_t hdr[BLK_SECTOR];
        if (blk_read(disk, 1, 1, hdr) != 0 || memcmp(hdr, "EFI PART", 8) != 0) return;
        uint64_t table = le64(hdr + 72);
        uint32_t count = le32(hdr + 80), esize = le32(hdr + 84);
        if (esize < 128 || esize > BLK_SECTOR || count > 128) return;
        int number = 0;
        for (uint32_t i = 0; i < count; i++) {
            uint64_t off = (uint64_t)i * esize;
            uint8_t ent[BLK_SECTOR];
            if (blk_read(disk, table + off / BLK_SECTOR, 1, ent) != 0) return;
            const uint8_t *e = ent + off % BLK_SECTOR;
            int used = 0;
            for (int k = 0; k < 16; k++) if (e[k]) used = 1;
            if (!used) continue;
            uint64_t first = le64(e + 32), last = le64(e + 40);
            add_part(disk, ++number, first, last - first + 1, gpt_kind(e));
        }
        return;
    }

    for (int i = 0; i < 4; i++) {
        const uint8_t *e = sec + 446 + i * 16;
        uint8_t type = e[4];
        if (type == 0) continue;
        add_part(disk, i + 1, le32(e + 8), le32(e + 12), mbr_kind(type));
    }
}
