/* NTFS (read-only) */

#ifndef _KERNEL_FS_NTFS_H
#define _KERNEL_FS_NTFS_H

struct blkdev;

/* Mount the NTFS volume on 'dev' at 'path'. Returns 0, or -1 if the device
 * does not hold a supported NTFS filesystem. */
int ntfs_mount(struct blkdev *dev, const char *path);

#endif
