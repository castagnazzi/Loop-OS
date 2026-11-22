/* Kernel Heap (kmalloc / kfree) */

#ifndef _KERNEL_MM_HEAP_H
#define _KERNEL_MM_HEAP_H

#include <stddef.h>
#include <stdint.h>

/* Initialize the heap (requires PMM and VMM). Grows on demand. */
void heap_init(void);

/* General purpose allocation. Returned memory is 16-byte aligned.
 * Not safe to call from interrupt handlers (no locking yet). */
void *kmalloc(size_t size);
void *kcalloc(size_t nmemb, size_t size);   /* zero-initialized */
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);

/* Statistics (bytes of payload) */
struct heap_stats {
    size_t total;       /* total payload bytes managed (free + used) */
    size_t used;        /* bytes in allocated blocks */
    size_t free;        /* bytes in free blocks */
    size_t blocks;      /* number of blocks (free + used) */
};
void heap_get_stats(struct heap_stats *out);

#endif
