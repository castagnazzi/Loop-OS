/* Initrd: ustar archive parser and read-only lookup.
 *
 * The archive is indexed once at boot into a table of {path, data, size}.
 * Directories are implicit: "bin" exists whenever some entry starts with
 * "bin/", so archives do not need explicit directory records.
 */

#include <kernel/fs/initrd.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

#define MAX_ENTRIES 256
#define PATH_MAX_LEN 160

struct entry {
    char path[PATH_MAX_LEN];     /* normalized: no leading "/" or "./" */
    const uint8_t *data;
    size_t size;
    int is_dir;                  /* explicit directory record in the archive */
};

static struct entry entries[MAX_ENTRIES];
static size_t entry_count = 0;
static int ready = 0;

/* tar header offsets */
#define TAR_NAME     0
#define TAR_SIZE     124
#define TAR_TYPE     156
#define TAR_MAGIC    257
#define TAR_PREFIX   345
#define TAR_BLOCK    512

static size_t parse_octal(const char *s, size_t n) {
    size_t v = 0;
    for (size_t i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++) {
        v = v * 8 + (size_t)(s[i] - '0');
    }
    return v;
}

/* Collapse "//", ".", ".." and strip leading/trailing "/". 'out' receives a
 * path like "bin/sh" (or "" for the root). */
static void normalize(const char *in, char *out, size_t out_max) {
    size_t len = 0;
    out[0] = '\0';

    while (*in) {
        while (*in == '/') in++;
        if (!*in) break;

        const char *start = in;
        while (*in && *in != '/') in++;
        size_t clen = (size_t)(in - start);

        if (clen == 1 && start[0] == '.') continue;
        if (clen == 2 && start[0] == '.' && start[1] == '.') {
            /* drop the last component */
            while (len > 0 && out[len - 1] != '/') len--;
            if (len > 0) len--;          /* the '/' itself */
            out[len] = '\0';
            continue;
        }

        if (len + clen + 2 > out_max) break;     /* too long: truncate */
        if (len > 0) out[len++] = '/';
        memcpy(out + len, start, clen);
        len += clen;
        out[len] = '\0';
    }
}

void initrd_canonical(const char *in, char *out, size_t out_max) {
    char norm[PATH_MAX_LEN];
    normalize(in, norm, sizeof(norm));
    size_t n = strlen(norm);
    if (n + 2 > out_max) n = out_max - 2;
    out[0] = '/';
    memcpy(out + 1, norm, n);
    out[1 + n] = '\0';
}

int initrd_init(const void *base, size_t size) {
    ready = 0;
    entry_count = 0;
    const uint8_t *p = base;
    size_t off = 0;

    while (off + TAR_BLOCK <= size && entry_count < MAX_ENTRIES) {
        const char *hdr = (const char *)(p + off);

        /* Two zero blocks end the archive; one is enough for us */
        if (hdr[TAR_NAME] == '\0') break;
        if (memcmp(hdr + TAR_MAGIC, "ustar", 5) != 0) {
            kprintf("initrd: bad tar header at offset %lu\n", (unsigned long)off);
            return -1;
        }

        size_t fsize = parse_octal(hdr + TAR_SIZE, 12);
        char type = hdr[TAR_TYPE];

        /* Full name = prefix + "/" + name when a prefix is present */
        char full[PATH_MAX_LEN * 2];
        full[0] = '\0';
        if (hdr[TAR_PREFIX] != '\0') {
            strncpy(full, hdr + TAR_PREFIX, 155);
            full[155] = '\0';
            strcat(full, "/");
        }
        size_t flen = strlen(full);
        strncpy(full + flen, hdr + TAR_NAME, 100);
        full[flen + 100] = '\0';

        if (type == '0' || type == '\0' || type == '5') {
            struct entry *e = &entries[entry_count];
            normalize(full, e->path, PATH_MAX_LEN);
            e->is_dir = (type == '5');
            e->data = e->is_dir ? NULL : p + off + TAR_BLOCK;
            e->size = e->is_dir ? 0 : fsize;
            if (e->path[0] != '\0') entry_count++;   /* skip the "./" root record */
        }

        off += TAR_BLOCK + ((fsize + TAR_BLOCK - 1) / TAR_BLOCK) * TAR_BLOCK;
    }

    ready = 1;
    return (int)entry_count;
}

int initrd_ready(void) { return ready; }

/* True if some entry lives below directory 'dir' ("" = root) */
static int dir_has_children(const char *dir) {
    size_t dl = strlen(dir);
    for (size_t i = 0; i < entry_count; i++) {
        const char *p = entries[i].path;
        if (dl == 0) return 1;
        if (strncmp(p, dir, dl) == 0 && p[dl] == '/') return 1;
    }
    return 0;
}

int initrd_lookup(const char *path, struct initrd_node *out) {
    if (!ready) return -1;
    char norm[PATH_MAX_LEN];
    normalize(path, norm, sizeof(norm));

    for (size_t i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].path, norm) == 0 && !entries[i].is_dir) {
            out->data = entries[i].data;
            out->size = entries[i].size;
            out->is_dir = 0;
            return 0;
        }
    }
    if (norm[0] == '\0' || dir_has_children(norm)) {
        out->data = NULL;
        out->size = 0;
        out->is_dir = 1;
        return 0;
    }
    return -1;
}

int initrd_readdir(const char *dir, size_t index, char *name, size_t name_max, int *is_dir) {
    if (!ready) return -1;
    char norm[PATH_MAX_LEN];
    normalize(dir, norm, sizeof(norm));
    size_t dl = strlen(norm);

    size_t seen = 0;
    for (size_t i = 0; i < entry_count; i++) {
        const char *p = entries[i].path;
        if (dl > 0) {
            if (strncmp(p, norm, dl) != 0 || p[dl] != '/') continue;
            p += dl + 1;
        }
        if (*p == '\0') continue;

        /* Next path component below 'dir' */
        const char *slash = strchr(p, '/');
        size_t clen = slash ? (size_t)(slash - p) : strlen(p);

        /* Report each name once (a directory shows up for every child) */
        int duplicate = 0;
        for (size_t j = 0; j < i; j++) {
            const char *q = entries[j].path;
            if (dl > 0) {
                if (strncmp(q, norm, dl) != 0 || q[dl] != '/') continue;
                q += dl + 1;
            }
            if (strncmp(q, p, clen) == 0 && (q[clen] == '/' || q[clen] == '\0')) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) continue;

        if (seen++ == index) {
            size_t n = clen < name_max - 1 ? clen : name_max - 1;
            memcpy(name, p, n);
            name[n] = '\0';
            if (is_dir) *is_dir = (slash != NULL) || entries[i].is_dir;
            return 0;
        }
    }
    return -1;
}
