/* ext2 / ext3 / ext4 driver (read-only).
 *
 * Handles classic block maps (ext2/ext3) and extents (ext4), 64-bit group
 * descriptors, linear and hashed directories, and short symlinks. The
 * journal is ignored (a volume that was not cleanly unmounted may show
 * stale data). Not supported: inline data, encryption, meta block groups. */

#include <kernel/blk.h>
#include <kernel/fs/vfs.h>
#include <kernel/fs/ext.h>
#include <kernel/mm/heap.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

#define EXT_MAGIC 0xEF53
#define INCOMPAT_FILETYPE 0x0002
#define INCOMPAT_64BIT    0x0080
#define INCOMPAT_UNSUPPORTED (0x0001 | 0x0008 | 0x0010 | 0x0100 | 0x0400 | 0x1000 | 0x8000 | 0x10000)

#define FL_EXTENTS 0x00080000

struct ext {
    struct blkdev *dev;
    uint32_t bs, bs_sectors;
    uint32_t inodes_per_group, inode_size, desc_size;
    uint32_t first_data_block;
    int is64;
    struct { uint64_t blk; uint8_t *buf; } cache[8];   /* recently used blocks */
    int cnext;
};

struct enode {
    struct ext *fs;
    uint32_t ino;
    uint16_t mode;
    uint32_t flags;
    uint64_t size;
    uint8_t iblock[60];
};

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* Return a block through the cache (NULL on error). The pointer stays valid
 * until a few more blocks have been fetched. */
static const uint8_t *get_block(struct ext *fs, uint64_t blk) {
    for (int i = 0; i < 8; i++) {
        if (fs->cache[i].buf && fs->cache[i].blk == blk) return fs->cache[i].buf;
    }
    int slot = fs->cnext;
    fs->cnext = (fs->cnext + 1) % 8;
    if (!fs->cache[slot].buf) {
        fs->cache[slot].buf = kmalloc(fs->bs);
        if (!fs->cache[slot].buf) return NULL;
    }
    fs->cache[slot].blk = ~0ULL;
    if (blk_read(fs->dev, blk * fs->bs_sectors, fs->bs_sectors, fs->cache[slot].buf) != 0) return NULL;
    fs->cache[slot].blk = blk;
    return fs->cache[slot].buf;
}

/* ---------------------------------------------------------------- */
/* Logical block -> physical block (0 = hole) */
static uint64_t map_extents(struct ext *fs, const uint8_t *node, uint32_t lblk) {
    for (int depth_guard = 0; depth_guard < 6; depth_guard++) {
        if (le16(node) != 0xF30A) return 0;
        uint32_t entries = le16(node + 2), depth = le16(node + 6);
        if (depth == 0) {
            for (uint32_t i = 0; i < entries; i++) {
                const uint8_t *e = node + 12 + i * 12;
                uint32_t first = le32(e);
                uint32_t len = le16(e + 4);
                if (len > 32768) len -= 32768;             /* uninitialised: still maps */
                if (lblk >= first && lblk < first + len) {
                    uint64_t start = le32(e + 8) | ((uint64_t)le16(e + 6) << 32);
                    return start + (lblk - first);
                }
            }
            return 0;
        }
        /* Index node: the last index whose first block is <= lblk */
        const uint8_t *pick = NULL;
        for (uint32_t i = 0; i < entries; i++) {
            const uint8_t *e = node + 12 + i * 12;
            if (le32(e) <= lblk) pick = e;
        }
        if (!pick) return 0;
        uint64_t leaf = le32(pick + 4) | ((uint64_t)le16(pick + 8) << 32);
        node = get_block(fs, leaf);
        if (!node) return 0;
    }
    return 0;
}

static uint64_t indirect(struct ext *fs, uint64_t blk, uint32_t index) {
    if (blk == 0) return 0;
    const uint8_t *b = get_block(fs, blk);
    if (!b) return 0;
    return le32(b + index * 4);
}

static uint64_t map_block(struct ext *fs, struct enode *n, uint32_t lblk) {
    if (n->flags & FL_EXTENTS) return map_extents(fs, n->iblock, lblk);

    uint32_t per = fs->bs / 4;
    if (lblk < 12) return le32(n->iblock + lblk * 4);
    lblk -= 12;
    if (lblk < per) return indirect(fs, le32(n->iblock + 48), lblk);
    lblk -= per;
    if (lblk < per * per) {
        uint64_t l1 = indirect(fs, le32(n->iblock + 52), lblk / per);
        return indirect(fs, l1, lblk % per);
    }
    lblk -= per * per;
    uint64_t l1 = indirect(fs, le32(n->iblock + 56), lblk / (per * per));
    uint64_t l2 = indirect(fs, l1, (lblk / per) % per);
    return indirect(fs, l2, lblk % per);
}

static long ext_read(struct vnode *v, size_t off, void *buf, size_t count) {
    struct enode *n = v->priv;
    struct ext *fs = n->fs;
    if (off >= n->size) return 0;
    if (count > n->size - off) count = (size_t)(n->size - off);

    /* Fast symlinks keep their target inside the inode */
    if ((n->mode & 0xF000) == 0xA000 && n->size < 60 && !(n->flags & FL_EXTENTS)) {
        memcpy(buf, n->iblock + off, count);
        return (long)count;
    }

    uint8_t *out = buf;
    size_t done = 0;
    while (done < count) {
        uint64_t pos = off + done;
        uint32_t lblk = (uint32_t)(pos / fs->bs);
        uint32_t in = (uint32_t)(pos % fs->bs);
        size_t c = fs->bs - in;
        if (c > count - done) c = count - done;

        uint64_t pblk = map_block(fs, n, lblk);
        if (pblk == 0) {
            memset(out + done, 0, c);
        } else {
            const uint8_t *b = get_block(fs, pblk);
            if (!b) break;
            memcpy(out + done, b + in, c);
        }
        done += c;
    }
    return (long)done;
}

/* ---------------------------------------------------------------- */
/* Inodes */
static int read_inode(struct ext *fs, uint32_t ino, struct enode *out) {
    if (ino == 0) return -1;
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t idx = (ino - 1) % fs->inodes_per_group;

    /* Group descriptor table starts in the block after the superblock */
    uint64_t gdt_blk = fs->first_data_block + 1;
    uint64_t gd_off = (uint64_t)group * fs->desc_size;
    const uint8_t *g = get_block(fs, gdt_blk + gd_off / fs->bs);
    if (!g) return -1;
    g += gd_off % fs->bs;
    uint64_t itable = le32(g + 8);
    if (fs->is64 && fs->desc_size >= 64) itable |= (uint64_t)le32(g + 0x28) << 32;

    uint64_t ioff = idx * (uint64_t)fs->inode_size;
    const uint8_t *in = get_block(fs, itable + ioff / fs->bs);
    if (!in) return -1;
    in += ioff % fs->bs;

    out->fs = fs;
    out->ino = ino;
    out->mode = le16(in);
    out->size = le32(in + 4) | ((uint64_t)le32(in + 0x6C) << 32);
    out->flags = le32(in + 0x20);
    memcpy(out->iblock, in + 0x28, 60);
    return 0;
}

static int ext_populate(struct vnode *dir);
static const struct vnode_ops ext_ops = { ext_read, ext_populate };

static void add_entry(struct vnode *dir, struct ext *fs, uint32_t ino, const char *name, int nlen) {
    struct enode tmp;
    if (read_inode(fs, ino, &tmp) != 0) return;
    int type = tmp.mode & 0xF000;
    if (type != 0x8000 && type != 0x4000 && type != 0xA000) return;   /* files, dirs, symlinks only */

    struct enode *n = kmalloc(sizeof(*n));
    if (!n) return;
    *n = tmp;

    /* UTF-8 on disk, Latin-1 inside Loop-OS ('?' for anything else) */
    char nm[VFS_NAME_MAX];
    int len = 0;
    for (int i = 0; i < nlen && len < VFS_NAME_MAX - 1; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x80) {
            nm[len++] = (char)c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < nlen) {
            unsigned cp = ((c & 0x1Fu) << 6) | ((unsigned char)name[i + 1] & 0x3Fu);
            nm[len++] = cp < 256 ? (char)cp : '?';
            i += 1;
        } else {
            nm[len++] = '?';
            while (i + 1 < nlen && ((unsigned char)name[i + 1] & 0xC0) == 0x80) i++;
        }
    }
    nm[len] = '\0';
    vfs_add_node(dir, nm, type == 0x4000, (size_t)n->size, &ext_ops, n);
}

static int ext_populate(struct vnode *dir) {
    struct enode *dn = dir->priv;
    struct ext *fs = dn->fs;
    uint8_t *blk = kmalloc(fs->bs);
    if (!blk) return -1;

    for (uint64_t off = 0; off < dn->size; off += fs->bs) {
        if (ext_read(dir, (size_t)off, blk, fs->bs) != (long)fs->bs) break;
        uint32_t p = 0;
        while (p + 8 <= fs->bs) {
            uint32_t ino = le32(blk + p);
            uint32_t rec = le16(blk + p + 4);
            int nlen = blk[p + 6];
            if (rec < 8 || p + rec > fs->bs || (uint32_t)nlen + 8 > rec) break;
            if (ino != 0 && nlen > 0) {
                const char *name = (const char *)blk + p + 8;
                if (!((nlen == 1 && name[0] == '.') || (nlen == 2 && name[0] == '.' && name[1] == '.'))) {
                    if (!(nlen == 10 && !memcmp(name, "lost+found", 10))) add_entry(dir, fs, ino, name, nlen);
                }
            }
            p += rec;
        }
    }
    kfree(blk);
    return 0;
}

/* ---------------------------------------------------------------- */
int ext_mount(struct blkdev *dev, const char *path) {
    uint8_t sb[1024];
    if (blk_read(dev, 2, 2, sb) != 0) return -1;            /* superblock: bytes 1024..2047 */
    if (le16(sb + 56) != EXT_MAGIC) return -1;

    uint32_t log_bs = le32(sb + 24);
    if (log_bs > 6) return -1;
    uint32_t rev = le32(sb + 76);
    uint32_t incompat = rev >= 1 ? le32(sb + 96) : 0;
    if (incompat & INCOMPAT_UNSUPPORTED) {
        kprintf("ext: %s uses unsupported features (0x%x)\n", dev->name, incompat);
        return -1;
    }

    struct ext *fs = kmalloc(sizeof(*fs));
    if (!fs) return -1;
    memset(fs, 0, sizeof(*fs));
    fs->dev = dev;
    fs->bs = 1024u << log_bs;
    fs->bs_sectors = fs->bs / BLK_SECTOR;
    fs->first_data_block = le32(sb + 20);
    fs->inodes_per_group = le32(sb + 40);
    fs->inode_size = rev >= 1 ? le16(sb + 88) : 128;
    fs->is64 = (incompat & INCOMPAT_64BIT) != 0;
    fs->desc_size = fs->is64 ? le16(sb + 254) : 32;
    if (fs->desc_size < 32) fs->desc_size = 32;
    if (fs->inodes_per_group == 0 || fs->inode_size < 128 || fs->inode_size > fs->bs) goto fail;

    struct enode *root = kmalloc(sizeof(*root));
    if (!root) goto fail;
    if (read_inode(fs, 2, root) != 0 || (root->mode & 0xF000) != 0x4000) { kfree(root); goto fail; }
    return vfs_mount(path, &ext_ops, root) ? 0 : -1;

fail:
    kfree(fs);
    return -1;
}
