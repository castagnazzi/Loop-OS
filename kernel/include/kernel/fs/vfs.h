/* VFS: a writable in-memory file system (ramfs).
 *
 * At boot the initrd archive is copied into a tree of vnodes, so everything
 * can be created, changed and removed. Nothing is persistent yet. */

#ifndef _KERNEL_FS_VFS_H
#define _KERNEL_FS_VFS_H

#include <stddef.h>
#include <stdint.h>

#define VFS_NAME_MAX 48

struct vnode {
    char name[VFS_NAME_MAX];
    int is_dir;
    uint8_t *data;             /* files: contents (heap) */
    size_t size, cap;
    int refs;                  /* open file descriptors */
    int unlinked;              /* removed from the tree, freed on last close */
    struct vnode *parent, *child, *next;
};

/* Copy the initrd into the tree. Call after initrd_init(). */
void vfs_init(void);

/* Canonical absolute form of a path ("/a/b", "/" for the root) */
void vfs_canonical(const char *in, char *out, size_t out_max);

struct vnode *vfs_lookup(const char *path);
/* Create a file or directory; NULL if it exists, the parent is missing, or
 * memory ran out. */
struct vnode *vfs_create(const char *path, int is_dir);
/* Remove a file or an empty directory. 0 on success, negative errno. */
int vfs_unlink(const char *path);

long vfs_read(struct vnode *n, size_t off, void *buf, size_t count);
long vfs_write(struct vnode *n, size_t off, const void *buf, size_t count);
void vfs_truncate(struct vnode *n);

/* The index-th entry of a directory, or NULL */
struct vnode *vfs_entry_at(struct vnode *dir, size_t index);

void vfs_ref(struct vnode *n);
void vfs_unref(struct vnode *n);

#endif
