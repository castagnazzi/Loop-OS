/* Block devices: disks and the partitions found on them.
 *
 * Sectors are 512 bytes. A partition is a block device of its own that
 * forwards reads to its parent with an offset. */

#ifndef _KERNEL_BLK_H
#define _KERNEL_BLK_H

#include <stdint.h>

#define BLK_SECTOR 512
#define BLK_MAX 16

struct blkdev {
    char name[16];                  /* "hda", "hda1" */
    char model[41];                 /* disks only */
    uint64_t sectors;
    int (*read)(struct blkdev *dev, uint64_t lba, uint32_t count, void *buf);
    void *priv;
    struct blkdev *parent;          /* partitions: the disk */
    uint64_t offset;                /* partitions: first sector on the disk */
    char kind[24];                  /* partitions: "FAT32", "Linux", "NTFS/exFAT"... */
    char fs[12];                    /* filesystem found by the probe, "" if none */
    char mount[32];                 /* where it is mounted, "" if not */
};

/* Add a device to the table; returns it (or NULL when the table is full) */
struct blkdev *blk_add(const struct blkdev *proto);
int blk_count(void);
struct blkdev *blk_get(int index);

/* Read 'count' sectors starting at 'lba'. 0 on success, -1 on error. */
int blk_read(struct blkdev *dev, uint64_t lba, uint32_t count, void *buf);

/* Hardware drivers and discovery */
void ata_init(void);                    /* register the IDE/ATA disks */
void part_scan(struct blkdev *disk);    /* register the partitions of a disk */
void storage_init(void);                /* all of the above plus mounting */

#endif
