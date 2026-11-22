/* Initrd: read-only filesystem served from a ustar archive in memory */

#ifndef _KERNEL_FS_INITRD_H
#define _KERNEL_FS_INITRD_H

#include <stddef.h>
#include <stdint.h>

struct initrd_node {
    const uint8_t *data;   /* file contents (NULL for directories) */
    size_t size;
    int is_dir;
};

/* Index the archive at 'base' (virtual address). Returns the number of
 * entries found, or -1 if it is not a usable archive. */
int initrd_init(const void *base, size_t size);
int initrd_ready(void);

/* Write the canonical absolute form of 'in' ("/a/b", "/" for the root) */
void initrd_canonical(const char *in, char *out, size_t out_max);

/* Find a file or directory. Paths may be absolute ("/bin/sh") or relative to
 * the root, and may contain "." and ".." components. Returns 0 on success. */
int initrd_lookup(const char *path, struct initrd_node *out);

/* Enumerate a directory. Copies the name of the 'index'-th entry into
 * 'name'. Returns 0 on success and -1 once the entries run out (or if 'dir'
 * is not a directory). */
int initrd_readdir(const char *dir, size_t index, char *name, size_t name_max, int *is_dir);

#endif
