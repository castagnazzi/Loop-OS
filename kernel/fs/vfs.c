/* VFS: writable in-memory file system */

#include <kernel/fs/vfs.h>
#include <kernel/fs/initrd.h>
#include <kernel/mm/heap.h>
#include <kernel/stdio.h>
#include <kernel/string.h>

#define ENOENT 2
#define ENOTEMPTY 39
#define EBUSY 16
#define EINVAL 22
#define MAX_FILE_SIZE (16u * 1024 * 1024)

static struct vnode *root = NULL;

static struct vnode *node_new(const char *name, int is_dir) {
    struct vnode *n = kmalloc(sizeof(*n));
    if (!n) return NULL;
    memset(n, 0, sizeof(*n));
    strncpy(n->name, name, VFS_NAME_MAX - 1);
    n->is_dir = is_dir;
    return n;
}

static void node_free(struct vnode *n) {
    if (n->data) kfree(n->data);
    kfree(n);
}

void vfs_canonical(const char *in, char *out, size_t out_max) {
    initrd_canonical(in, out, out_max);   /* same normalisation rules */
}

/* Look up one name in a directory */
static struct vnode *find_child(struct vnode *dir, const char *name, size_t len) {
    for (struct vnode *c = dir->child; c; c = c->next) {
        if (strlen(c->name) == len && strncmp(c->name, name, len) == 0) return c;
    }
    return NULL;
}

/* Resolve the parent directory of a canonical path and return the last name */
static struct vnode *walk(const char *canon, const char **last_out, int stop_at_parent) {
    struct vnode *cur = root;
    const char *p = canon;
    while (*p == '/') p++;
    while (*p) {
        const char *s = p;
        while (*p && *p != '/') p++;
        size_t len = (size_t)(p - s);
        while (*p == '/') p++;
        if (stop_at_parent && !*p) {
            *last_out = s;
            return cur;
        }
        if (!cur->is_dir) return NULL;
        cur = find_child(cur, s, len);
        if (!cur) return NULL;
    }
    if (stop_at_parent) { *last_out = p; return NULL; }
    return cur;
}

struct vnode *vfs_lookup(const char *path) {
    char canon[160];
    vfs_canonical(path, canon, sizeof(canon));
    return walk(canon, NULL, 0);
}

struct vnode *vfs_create(const char *path, int is_dir) {
    char canon[160];
    vfs_canonical(path, canon, sizeof(canon));
    const char *last;
    struct vnode *parent = walk(canon, &last, 1);
    if (!parent || !parent->is_dir || !*last) return NULL;
    size_t len = strlen(last);
    if (len >= VFS_NAME_MAX || find_child(parent, last, len)) return NULL;

    struct vnode *n = node_new(last, is_dir);
    if (!n) return NULL;
    n->parent = parent;
    /* append, so listings keep creation order */
    struct vnode **pp = &parent->child;
    while (*pp) pp = &(*pp)->next;
    *pp = n;
    return n;
}

int vfs_unlink(const char *path) {
    struct vnode *n = vfs_lookup(path);
    if (!n) return -ENOENT;
    if (n == root) return -EBUSY;
    if (n->is_dir && n->child) return -ENOTEMPTY;
    struct vnode **pp = &n->parent->child;
    while (*pp && *pp != n) pp = &(*pp)->next;
    if (*pp) *pp = n->next;
    n->parent = NULL;
    if (n->refs > 0) n->unlinked = 1;
    else node_free(n);
    return 0;
}

void vfs_ref(struct vnode *n) { if (n) n->refs++; }

void vfs_unref(struct vnode *n) {
    if (!n) return;
    if (n->refs > 0) n->refs--;
    if (n->refs == 0 && n->unlinked) node_free(n);
}

static int reserve(struct vnode *n, size_t need) {
    if (need <= n->cap) return 0;
    if (need > MAX_FILE_SIZE) return -1;
    size_t ncap = n->cap ? n->cap : 256;
    while (ncap < need) ncap *= 2;
    uint8_t *nd = kmalloc(ncap);
    if (!nd) return -1;
    if (n->size) memcpy(nd, n->data, n->size);
    if (n->data) kfree(n->data);
    n->data = nd;
    n->cap = ncap;
    return 0;
}

long vfs_read(struct vnode *n, size_t off, void *buf, size_t count) {
    if (n->is_dir || off >= n->size) return 0;
    size_t left = n->size - off;
    if (count > left) count = left;
    memcpy(buf, n->data + off, count);
    return (long)count;
}

long vfs_write(struct vnode *n, size_t off, const void *buf, size_t count) {
    if (n->is_dir) return -EINVAL;
    if (off > n->size) {                       /* sparse gap reads as zeros */
        if (reserve(n, off) != 0) return -12;
        memset(n->data + n->size, 0, off - n->size);
        n->size = off;
    }
    if (reserve(n, off + count) != 0) return -12;
    memcpy(n->data + off, buf, count);
    if (off + count > n->size) n->size = off + count;
    return (long)count;
}

void vfs_truncate(struct vnode *n) {
    n->size = 0;
}

struct vnode *vfs_entry_at(struct vnode *dir, size_t index) {
    if (!dir || !dir->is_dir) return NULL;
    struct vnode *c = dir->child;
    while (c && index--) c = c->next;
    return c;
}

/* ---------------------------------------------------------------- */
static void copy_dir(struct vnode *dst, const char *path) {
    char name[VFS_NAME_MAX];
    int is_dir;
    for (size_t i = 0; initrd_readdir(path, i, name, sizeof(name), &is_dir) == 0; i++) {
        char child[160];
        size_t pl = strlen(path);
        strncpy(child, path, sizeof(child) - 2);
        child[sizeof(child) - 2] = '\0';
        if (pl == 0 || child[pl - 1] != '/') strcat(child, "/");
        strncat(child, name, sizeof(child) - strlen(child) - 1);

        struct vnode *n = vfs_create(child, is_dir);
        if (!n) continue;
        if (is_dir) {
            copy_dir(n, child);
        } else {
            struct initrd_node in;
            if (initrd_lookup(child, &in) == 0 && in.size) vfs_write(n, 0, in.data, in.size);
        }
    }
}

void vfs_init(void) {
    root = node_new("", 1);
    if (!root) return;
    if (initrd_ready()) copy_dir(root, "/");
    if (!vfs_lookup("/tmp")) vfs_create("/tmp", 1);
    kprintf("vfs: ramfs ready\n");
}
