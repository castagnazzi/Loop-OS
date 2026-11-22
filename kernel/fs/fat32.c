/* FAT32 driver (read-only, with long file names). */

#include <kernel/blk.h>
#include <kernel/fs/vfs.h>
#include <kernel/fs/fat32.h>
#include <kernel/mm/heap.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

struct fat {
    struct blkdev *dev;
    uint32_t spc;                  /* sectors per cluster */
    uint32_t fat_lba, data_lba, root;
    uint32_t clusters;
    uint8_t fatbuf[BLK_SECTOR];
    uint64_t fatbuf_lba;
    uint8_t datbuf[BLK_SECTOR];
    uint64_t datbuf_lba;
};

struct fnode {
    struct fat *fs;
    uint32_t first;                /* first cluster */
    uint32_t cache_idx, cache_cl;  /* last cluster visited, to speed up sequential reads */
};

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

#define EOC 0x0FFFFFF8u

static uint64_t cluster_lba(struct fat *f, uint32_t cl) {
    return (uint64_t)f->data_lba + (uint64_t)(cl - 2) * f->spc;
}

static int valid_cluster(struct fat *f, uint32_t cl) {
    return cl >= 2 && cl < f->clusters + 2;
}

static uint32_t fat_next(struct fat *f, uint32_t cl) {
    uint64_t lba = f->fat_lba + (cl * 4) / BLK_SECTOR;
    if (f->fatbuf_lba != lba) {
        if (blk_read(f->dev, lba, 1, f->fatbuf) != 0) return EOC;
        f->fatbuf_lba = lba;
    }
    return le32(f->fatbuf + (cl * 4) % BLK_SECTOR) & 0x0FFFFFFF;
}

/* ---------------------------------------------------------------- */
static long fat_read(struct vnode *n, size_t off, void *buf, size_t count) {
    struct fnode *fn = n->priv;
    struct fat *f = fn->fs;
    if (off >= n->size) return 0;
    if (count > n->size - off) count = n->size - off;

    uint32_t cbytes = f->spc * BLK_SECTOR;
    uint32_t idx = (uint32_t)(off / cbytes);
    uint32_t cl, at;
    if (fn->cache_cl && fn->cache_idx <= idx) { cl = fn->cache_cl; at = fn->cache_idx; }
    else { cl = fn->first; at = 0; }
    while (at < idx) {
        cl = fat_next(f, cl);
        if (!valid_cluster(f, cl)) return 0;
        at++;
    }

    uint8_t *out = buf;
    size_t done = 0;
    while (done < count) {
        if (!valid_cluster(f, cl)) break;
        fn->cache_idx = at;
        fn->cache_cl = cl;

        uint32_t in_cluster = (uint32_t)((off + done) % cbytes);
        uint64_t lba = cluster_lba(f, cl) + in_cluster / BLK_SECTOR;
        uint32_t in_sector = in_cluster % BLK_SECTOR;
        if (f->datbuf_lba != lba) {
            if (blk_read(f->dev, lba, 1, f->datbuf) != 0) break;
            f->datbuf_lba = lba;
        }
        size_t chunk = BLK_SECTOR - in_sector;
        if (chunk > count - done) chunk = count - done;
        memcpy(out + done, f->datbuf + in_sector, chunk);
        done += chunk;

        if ((off + done) % cbytes == 0 && done < count) {
            cl = fat_next(f, cl);
            at++;
        }
    }
    return (long)done;
}

static int fat_populate(struct vnode *dir);
static const struct vnode_ops fat_ops = { fat_read, fat_populate };

/* ---------------------------------------------------------------- */
static void add_entry(struct vnode *dir, struct fat *f, const char *name, int is_dir,
                      uint32_t cluster, uint32_t size) {
    struct fnode *fn = kmalloc(sizeof(*fn));
    if (!fn) return;
    memset(fn, 0, sizeof(*fn));
    fn->fs = f;
    fn->first = cluster;
    vfs_add_node(dir, name, is_dir, size, &fat_ops, fn);
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int fat_populate(struct vnode *dir) {
    struct fnode *dn = dir->priv;
    struct fat *f = dn->fs;
    uint32_t cbytes = f->spc * BLK_SECTOR;
    uint8_t *buf = kmalloc(cbytes);
    if (!buf) return -1;

    char lfn[260];
    int have_lfn = 0;
    uint32_t cl = dn->first;
    int guard = 0;

    while (valid_cluster(f, cl) && guard++ < 100000) {
        if (blk_read(f->dev, cluster_lba(f, cl), f->spc, buf) != 0) break;
        for (uint32_t off = 0; off < cbytes; off += 32) {
            const uint8_t *e = buf + off;
            if (e[0] == 0x00) { kfree(buf); return 0; }     /* end of directory */
            if (e[0] == 0xE5) { have_lfn = 0; continue; }
            uint8_t attr = e[11];

            if (attr == 0x0F) {                              /* long name piece */
                int ord = e[0] & 0x1F;
                if (ord < 1 || ord > 20) { have_lfn = 0; continue; }
                if (e[0] & 0x40) { memset(lfn, 0, sizeof(lfn)); have_lfn = 1; }
                static const uint8_t pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
                for (int i = 0; i < 13; i++) {
                    uint16_t ch = le16(e + pos[i]);
                    int at = (ord - 1) * 13 + i;
                    if (at < 255) lfn[at] = (ch == 0 || ch == 0xFFFF) ? 0 : (ch < 256 ? (char)ch : '?');
                }
                continue;
            }
            if (attr & 0x08) { have_lfn = 0; continue; }     /* volume label */

            char name[64];
            if (have_lfn) {
                strncpy(name, lfn, sizeof(name) - 1);
                name[sizeof(name) - 1] = '\0';
            } else {
                int n = 0;
                int lc_base = e[12] & 0x08, lc_ext = e[12] & 0x10;
                for (int i = 0; i < 8 && e[i] != ' '; i++) name[n++] = lc_base ? lower((char)e[i]) : (char)e[i];
                if (e[8] != ' ') {
                    name[n++] = '.';
                    for (int i = 8; i < 11 && e[i] != ' '; i++) name[n++] = lc_ext ? lower((char)e[i]) : (char)e[i];
                }
                name[n] = '\0';
            }
            have_lfn = 0;
            if (!strcmp(name, ".") || !strcmp(name, "..") || !name[0]) continue;

            uint32_t cluster = ((uint32_t)le16(e + 20) << 16) | le16(e + 26);
            add_entry(dir, f, name, (attr & 0x10) != 0, cluster, le32(e + 28));
        }
        cl = fat_next(f, cl);
    }
    kfree(buf);
    return 0;
}

/* ---------------------------------------------------------------- */
int fat32_mount(struct blkdev *dev, const char *path) {
    uint8_t bs[BLK_SECTOR];
    if (blk_read(dev, 0, 1, bs) != 0) return -1;
    if (bs[510] != 0x55 || bs[511] != 0xAA) return -1;
    if (le16(bs + 11) != BLK_SECTOR) return -1;
    uint32_t spc = bs[13], reserved = le16(bs + 14), nfats = bs[16];
    uint32_t fatsz = le32(bs + 36), root = le32(bs + 44);
    if (spc == 0 || (spc & (spc - 1)) || reserved == 0 || nfats == 0) return -1;
    if (le16(bs + 22) != 0 || fatsz == 0) return -1;           /* FAT12/16 */
    if (memcmp(bs + 82, "FAT32", 5) != 0) return -1;

    uint32_t total = le16(bs + 19) ? le16(bs + 19) : le32(bs + 32);
    uint32_t data_start = reserved + nfats * fatsz;
    if (total <= data_start) return -1;

    struct fat *f = kmalloc(sizeof(*f));
    struct fnode *rn = kmalloc(sizeof(*rn));
    if (!f || !rn) return -1;
    memset(f, 0, sizeof(*f));
    memset(rn, 0, sizeof(*rn));
    f->dev = dev;
    f->spc = spc;
    f->fat_lba = reserved;
    f->data_lba = data_start;
    f->root = root;
    f->clusters = (total - data_start) / spc;
    f->fatbuf_lba = f->datbuf_lba = ~0ULL;
    rn->fs = f;
    rn->first = root;

    struct vnode *v = vfs_mount(path, &fat_ops, rn);
    return v ? 0 : -1;
}
