/* ext2 / ext3 / ext4 (read-only) */

#ifndef _KERNEL_FS_EXT_H
#define _KERNEL_FS_EXT_H

struct blkdev;

/* Mount the ext2/3/4 volume on 'dev' at 'path'. Returns 0, or -1 if the
 * device does not hold a supported ext filesystem. */
int ext_mount(struct blkdev *dev, const char *path);

#endif
