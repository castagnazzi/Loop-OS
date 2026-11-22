/* FAT32 (read-only) */

#ifndef _KERNEL_FS_FAT32_H
#define _KERNEL_FS_FAT32_H

struct blkdev;

/* Mount the FAT32 volume on 'dev' at 'path' (e.g. "/mnt/hda1").
 * Returns 0, or -1 if the device does not hold a FAT32 filesystem. */
int fat32_mount(struct blkdev *dev, const char *path);

#endif
