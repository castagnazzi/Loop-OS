/* Storage discovery: disks, partitions, filesystem probing and mounting.
 * Writes a summary to /etc/mounts (read it with `cat /etc/mounts`). */

#include <kernel/blk.h>
#include <kernel/fs/vfs.h>
#include <kernel/fs/fat32.h>
#include <kernel/fs/ntfs.h>
#include <kernel/fs/ext.h>
#include <kernel/stdio.h>
#include <kernel/string.h>

static void try_mount(struct blkdev *d) {
    char path[32];
    path[0] = '\0';
    strcpy(path, "/mnt/");
    strcat(path, d->name);
    const char *fsname = NULL;
    if (fat32_mount(d, path) == 0) fsname = "fat32";
    else if (ntfs_mount(d, path) == 0) fsname = "ntfs";
    else if (ext_mount(d, path) == 0) fsname = "ext";
    if (fsname) {
        strcpy(d->fs, fsname);
        strcpy(d->mount, path);
        kprintf("fs: %s mounted at %s (%s, read-only)\n", d->name, path, fsname);
    }
}

static void append(char *buf, size_t max, const char *s) {
    size_t n = strlen(buf);
    strncpy(buf + n, s, max - n - 1);
    buf[max - 1] = '\0';
}

static void write_report(void) {
    static char text[1024];
    text[0] = '\0';
    append(text, sizeof(text), "device  size(MB)  type          filesystem  mounted at\n");
    for (int i = 0; i < blk_count(); i++) {
        struct blkdev *d = blk_get(i);
        char line[128];
        ksnprintf(line, sizeof(line), "%-7s %-9lu %-13s %-11s %s\n", d->name,
                  (unsigned long)(d->sectors / 2048),
                  d->parent ? d->kind : (d->model[0] ? d->model : "disk"),
                  d->fs[0] ? d->fs : "-", d->mount[0] ? d->mount : "-");
        append(text, sizeof(text), line);
    }
    struct vnode *n = vfs_lookup("/etc/mounts");
    if (!n) n = vfs_create("/etc/mounts", 0);
    if (n) {
        vfs_truncate(n);
        vfs_write(n, 0, text, strlen(text));
    }
}

void storage_init(void) {
    ata_init();

    int disks = blk_count();
    for (int i = 0; i < disks; i++) part_scan(blk_get(i));

    /* A disk may hold a filesystem directly (no partition table), so every
     * device is probed, partitions included. */
    for (int i = 0; i < blk_count(); i++) try_mount(blk_get(i));
    write_report();
}
