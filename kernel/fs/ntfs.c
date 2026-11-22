/* NTFS driver (read-only).
 *
 * Reads the MFT, directories (resident index root plus INDEX_ALLOCATION
 * blocks) and file data (resident, non-resident and sparse). Not supported:
 * compressed or encrypted files and files whose attributes spill into
 * extension records ($ATTRIBUTE_LIST). File names are shown as Latin-1. */

#include <kernel/blk.h>
#include <kernel/fs/vfs.h>
#include <kernel/fs/ntfs.h>
#include <kernel/mm/heap.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

#define ATTR_FILE_NAME   0x30
#define ATTR_DATA        0x80
#define ATTR_INDEX_ROOT  0x90
#define ATTR_INDEX_ALLOC 0xA0
#define ATTR_LIST        0x20
#define ATTR_END         0xFFFFFFFFu

struct run { uint64_t vcn, len, lcn; };      /* lcn 0 = sparse */

struct nstream {
    int resident;
    uint8_t *rdata;
    uint64_t size;
    struct run *runs;
    uint32_t nruns, last;
    int unsupported;
};

struct ntfs {
    struct blkdev *dev;
    uint32_t cluster, rec_size, idx_size;
    uint64_t mft_lcn;
    struct nstream mft;
    uint8_t sec[BLK_SECTOR];
    uint64_t sec_lba;
};

struct nnode {
    struct ntfs *fs;
    uint64_t mft_no;
    int loaded;
    struct nstream stream;
};

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t le64(const uint8_t *p) { return le32(p) | ((uint64_t)le32(p + 4) << 32); }

/* ---------------------------------------------------------------- */
/* Raw volume access */
static int vol_read(struct ntfs *fs, uint64_t off, void *buf, size_t n) {
    uint8_t *out = buf;
    while (n) {
        uint64_t lba = off / BLK_SECTOR;
        uint32_t in = (uint32_t)(off % BLK_SECTOR);
        if (in == 0 && n >= BLK_SECTOR) {
            uint32_t cnt = (uint32_t)(n / BLK_SECTOR);
            if (blk_read(fs->dev, lba, cnt, out) != 0) return -1;
            size_t bytes = (size_t)cnt * BLK_SECTOR;
            out += bytes; off += bytes; n -= bytes;
            continue;
        }
        if (fs->sec_lba != lba) {
            if (blk_read(fs->dev, lba, 1, fs->sec) != 0) return -1;
            fs->sec_lba = lba;
        }
        size_t c = BLK_SECTOR - in;
        if (c > n) c = n;
        memcpy(out, fs->sec + in, c);
        out += c; off += c; n -= c;
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* Streams: attribute contents, resident or described by a run list */
static void stream_free(struct nstream *s) {
    if (s->rdata) kfree(s->rdata);
    if (s->runs) kfree(s->runs);
    memset(s, 0, sizeof(*s));
}

static size_t stream_read(struct ntfs *fs, struct nstream *s, uint64_t off, void *buf, size_t n) {
    if (s->unsupported || off >= s->size) return 0;
    if (n > s->size - off) n = (size_t)(s->size - off);

    if (s->resident) {
        memcpy(buf, s->rdata + off, n);
        return n;
    }

    uint8_t *out = buf;
    size_t done = 0;
    uint32_t i = s->last;
    while (done < n) {
        uint64_t pos = off + done;
        uint64_t vcn = pos / fs->cluster;
        if (i >= s->nruns || s->runs[i].vcn > vcn) i = 0;
        while (i < s->nruns && vcn >= s->runs[i].vcn + s->runs[i].len) i++;
        if (i >= s->nruns) break;
        s->last = i;

        uint64_t in_run = pos - s->runs[i].vcn * fs->cluster;
        uint64_t avail = s->runs[i].len * fs->cluster - in_run;
        size_t c = n - done;
        if (c > avail) c = (size_t)avail;
        if (s->runs[i].lcn == 0) memset(out + done, 0, c);
        else if (vol_read(fs, s->runs[i].lcn * fs->cluster + in_run, out + done, c) != 0) break;
        done += c;
    }
    return done;
}

static int apply_fixups(uint8_t *buf, uint32_t size) {
    uint32_t uoff = le16(buf + 4), ucnt = le16(buf + 6);
    if (ucnt < 2 || uoff + ucnt * 2 > size) return -1;
    uint16_t usn = le16(buf + uoff);
    for (uint32_t i = 1; i < ucnt; i++) {
        uint32_t pos = i * BLK_SECTOR - 2;
        if (pos + 2 > size) break;
        if (le16(buf + pos) != usn) return -1;
        buf[pos] = buf[uoff + 2 * i];
        buf[pos + 1] = buf[uoff + 2 * i + 1];
    }
    return 0;
}

/* Build a stream from an attribute header inside a record */
static int stream_from_attr(struct ntfs *fs, const uint8_t *attr, struct nstream *out) {
    memset(out, 0, sizeof(*out));
    uint32_t alen = le32(attr + 4);

    if (attr[8] == 0) {                                  /* resident */
        uint32_t len = le32(attr + 0x10), off = le16(attr + 0x14);
        if (off + len > alen) return -1;
        out->rdata = kmalloc(len ? len : 1);
        if (!out->rdata) return -1;
        memcpy(out->rdata, attr + off, len);
        out->resident = 1;
        out->size = len;
        return 0;
    }

    uint16_t flags = le16(attr + 0x0C);
    if (flags & 0x4001) out->unsupported = 1;            /* compressed or encrypted */
    out->size = le64(attr + 0x30);
    uint32_t roff = le16(attr + 0x20);
    if (roff >= alen) return -1;

    /* Count runs, then decode them */
    for (int pass = 0; pass < 2; pass++) {
        const uint8_t *p = attr + roff, *end = attr + alen;
        int64_t lcn = 0;
        uint64_t vcn = 0;
        uint32_t k = 0;
        while (p < end && *p) {
            int lb = *p & 15, ob = *p >> 4;
            p++;
            if (lb == 0 || lb > 8 || ob > 8 || p + lb + ob > end) break;
            uint64_t len = 0;
            for (int i = 0; i < lb; i++) len |= (uint64_t)p[i] << (8 * i);
            p += lb;
            uint64_t lcn_out = 0;
            if (ob) {
                int64_t d = 0;
                for (int i = 0; i < ob; i++) d |= (int64_t)((uint64_t)p[i] << (8 * i));
                if (ob < 8 && (p[ob - 1] & 0x80)) d |= -(int64_t)1 << (8 * ob);
                p += ob;
                lcn += d;
                lcn_out = (uint64_t)lcn;
            }
            if (pass == 1) {
                out->runs[k].vcn = vcn;
                out->runs[k].len = len;
                out->runs[k].lcn = lcn_out;
            }
            k++;
            vcn += len;
        }
        if (pass == 0) {
            out->nruns = k;
            out->runs = kmalloc(sizeof(struct run) * (k ? k : 1));
            if (!out->runs) return -1;
        }
    }
    return 0;
}

/* Find an attribute of 'type' (named like 'name', or unnamed when NULL) */
static const uint8_t *find_attr(const uint8_t *rec, uint32_t rec_size, uint32_t type, const char *name) {
    uint32_t off = le16(rec + 0x14);
    while (off + 16 <= rec_size) {
        const uint8_t *a = rec + off;
        uint32_t t = le32(a);
        if (t == ATTR_END) break;
        uint32_t len = le32(a + 4);
        if (len < 16 || off + len > rec_size) break;
        if (t == type) {
            int nl = a[9];
            if (!name && nl == 0) return a;
            if (name && (int)strlen(name) == nl) {
                const uint8_t *nm = a + le16(a + 0x0A);
                int same = 1;
                for (int i = 0; i < nl; i++) if (le16(nm + 2 * i) != (uint8_t)name[i]) same = 0;
                if (same) return a;
            }
        }
        off += len;
    }
    return NULL;
}

static int read_record(struct ntfs *fs, uint64_t no, uint8_t *buf) {
    if (stream_read(fs, &fs->mft, no * fs->rec_size, buf, fs->rec_size) != fs->rec_size) return -1;
    if (memcmp(buf, "FILE", 4) != 0) return -1;
    if (apply_fixups(buf, fs->rec_size) != 0) return -1;
    if (!(le16(buf + 0x16) & 1)) return -1;              /* not in use */
    return 0;
}

/* ---------------------------------------------------------------- */
static long ntfs_read(struct vnode *n, size_t off, void *buf, size_t count) {
    struct nnode *nn = n->priv;
    struct ntfs *fs = nn->fs;

    if (!nn->loaded) {
        nn->loaded = 1;
        uint8_t *rec = kmalloc(fs->rec_size);
        if (rec) {
            if (read_record(fs, nn->mft_no, rec) == 0 && !find_attr(rec, fs->rec_size, ATTR_LIST, NULL)) {
                const uint8_t *a = find_attr(rec, fs->rec_size, ATTR_DATA, NULL);
                if (a && stream_from_attr(fs, a, &nn->stream) == 0) n->size = nn->stream.size;
                else nn->stream.unsupported = 1;
            } else {
                nn->stream.unsupported = 1;
            }
            kfree(rec);
        }
    }
    return (long)stream_read(fs, &nn->stream, off, buf, count);
}

static int ntfs_populate(struct vnode *dir);
static const struct vnode_ops ntfs_ops = { ntfs_read, ntfs_populate };

static void add_entry(struct vnode *dir, struct ntfs *fs, const uint8_t *e, uint32_t elen) {
    if (elen < 0x52 || le16(e + 0x0A) < 0x42) return;
    const uint8_t *fn = e + 0x10;
    uint64_t mft_no = le64(e) & 0xFFFFFFFFFFFFULL;
    int nlen = fn[0x40], ns = fn[0x41];
    if (ns == 2 || nlen == 0 || mft_no < 24) return;      /* DOS names, system files */
    if (0x10 + 0x42 + (uint32_t)nlen * 2 > elen) return;

    char name[VFS_NAME_MAX];
    int n = 0;
    for (int i = 0; i < nlen && n < VFS_NAME_MAX - 1; i++) {
        uint16_t ch = le16(fn + 0x42 + 2 * i);
        name[n++] = ch < 256 ? (char)ch : '?';
    }
    name[n] = '\0';

    struct nnode *nn = kmalloc(sizeof(*nn));
    if (!nn) return;
    memset(nn, 0, sizeof(*nn));
    nn->fs = fs;
    nn->mft_no = mft_no;
    int is_dir = (le32(fn + 0x38) & 0x10000000) != 0;
    vfs_add_node(dir, name, is_dir, (size_t)le64(fn + 0x30), &ntfs_ops, nn);
}

static void parse_entries(struct vnode *dir, struct ntfs *fs, const uint8_t *p, const uint8_t *end) {
    while (p + 16 <= end) {
        uint32_t len = le16(p + 8);
        uint32_t flags = le16(p + 0x0C);
        if (len < 16 || p + len > end) break;
        if (flags & 2) break;                              /* last entry */
        add_entry(dir, fs, p, len);
        p += len;
    }
}

static int ntfs_populate(struct vnode *dir) {
    struct nnode *dn = dir->priv;
    struct ntfs *fs = dn->fs;
    uint8_t *rec = kmalloc(fs->rec_size);
    if (!rec) return -1;
    if (read_record(fs, dn->mft_no, rec) != 0) { kfree(rec); return -1; }

    const uint8_t *root = find_attr(rec, fs->rec_size, ATTR_INDEX_ROOT, "$I30");
    if (root && root[8] == 0) {
        const uint8_t *c = root + le16(root + 0x14);
        const uint8_t *nh = c + 0x10;
        uint32_t first = le32(nh), total = le32(nh + 4);
        parse_entries(dir, fs, nh + first, nh + total);
    }

    const uint8_t *alloc = find_attr(rec, fs->rec_size, ATTR_INDEX_ALLOC, "$I30");
    if (alloc) {
        struct nstream s;
        uint8_t *blk = kmalloc(fs->idx_size);
        if (blk && stream_from_attr(fs, alloc, &s) == 0) {
            for (uint64_t off = 0; off + fs->idx_size <= s.size; off += fs->idx_size) {
                if (stream_read(fs, &s, off, blk, fs->idx_size) != fs->idx_size) break;
                if (memcmp(blk, "INDX", 4) != 0 || apply_fixups(blk, fs->idx_size) != 0) continue;
                const uint8_t *nh = blk + 0x18;
                uint32_t first = le32(nh), total = le32(nh + 4);
                if (0x18 + total > fs->idx_size) continue;
                parse_entries(dir, fs, nh + first, nh + total);
            }
            stream_free(&s);
        }
        if (blk) kfree(blk);
    }
    kfree(rec);
    return 0;
}

/* ---------------------------------------------------------------- */
static uint32_t record_size(int8_t v, uint32_t cluster) {
    return v > 0 ? (uint32_t)v * cluster : 1u << (-v);
}

int ntfs_mount(struct blkdev *dev, const char *path) {
    uint8_t bs[BLK_SECTOR];
    if (blk_read(dev, 0, 1, bs) != 0) return -1;
    if (memcmp(bs + 3, "NTFS    ", 8) != 0) return -1;
    if (le16(bs + 11) != BLK_SECTOR) return -1;
    uint32_t spc = bs[13];
    if (spc == 0 || (spc & (spc - 1))) return -1;

    struct ntfs *fs = kmalloc(sizeof(*fs));
    if (!fs) return -1;
    memset(fs, 0, sizeof(*fs));
    fs->dev = dev;
    fs->cluster = spc * BLK_SECTOR;
    fs->mft_lcn = le64(bs + 0x30);
    fs->rec_size = record_size((int8_t)bs[0x40], fs->cluster);
    fs->idx_size = record_size((int8_t)bs[0x44], fs->cluster);
    fs->sec_lba = ~0ULL;
    if (fs->rec_size < 512 || fs->rec_size > 8192 || fs->idx_size < 512 || fs->idx_size > 65536) goto fail;

    /* Record 0 ($MFT) describes where the rest of the MFT lives */
    uint8_t *rec = kmalloc(fs->rec_size);
    if (!rec) goto fail;
    if (vol_read(fs, fs->mft_lcn * fs->cluster, rec, fs->rec_size) != 0 ||
        memcmp(rec, "FILE", 4) != 0 || apply_fixups(rec, fs->rec_size) != 0) { kfree(rec); goto fail; }
    const uint8_t *data = find_attr(rec, fs->rec_size, ATTR_DATA, NULL);
    int ok = data && stream_from_attr(fs, data, &fs->mft) == 0 && !fs->mft.unsupported;
    kfree(rec);
    if (!ok) goto fail;

    struct nnode *rn = kmalloc(sizeof(*rn));
    if (!rn) goto fail;
    memset(rn, 0, sizeof(*rn));
    rn->fs = fs;
    rn->mft_no = 5;                                       /* root directory */
    return vfs_mount(path, &ntfs_ops, rn) ? 0 : -1;

fail:
    kfree(fs);
    return -1;
}
