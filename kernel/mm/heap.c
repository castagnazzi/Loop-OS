/* Kernel Heap Implementation
 *
 * Address-ordered list of blocks, each with a 32-byte header:
 *   - first-fit allocation, blocks are split when the remainder is useful
 *   - kfree coalesces with the neighbouring free blocks
 *   - the heap grows by mapping more pages with vmm_alloc()
 *   - a magic value in every header catches bad pointers and double frees
 */

#include <kernel/mm/heap.h>
#include <kernel/mm/vmm.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

#define HEAP_MAGIC_USED 0xA110C8EDU
#define HEAP_MAGIC_FREE 0xF4EE0B10U
#define HEAP_ALIGN      16
#define HEAP_MIN_GROW   16                 /* pages (64 KiB) */

struct block {
    size_t size;            /* payload size in bytes (multiple of HEAP_ALIGN) */
    uint32_t magic;
    uint32_t pad;
    struct block *next;     /* next block by address */
    uint64_t pad2;          /* keep the header at 32 bytes (payload 16-aligned) */
};

#define HDR sizeof(struct block)

_Static_assert(sizeof(struct block) == 32, "heap header must be 32 bytes");

static struct block *heap_head = NULL;
static int heap_ready = 0;

static inline size_t align_up(size_t n) {
    return (n + (HEAP_ALIGN - 1)) & ~(size_t)(HEAP_ALIGN - 1);
}

static inline void *payload(struct block *b) {
    return (void *)((uint8_t *)b + HDR);
}

static inline struct block *header(void *p) {
    return (struct block *)((uint8_t *)p - HDR);
}

/* True if b and b->next are contiguous in memory */
static inline int adjacent(struct block *b) {
    return b->next && (uint8_t *)b + HDR + b->size == (uint8_t *)b->next;
}

/* Map more memory and append it to the list. Returns 0 on success. */
static int heap_grow(size_t min_payload) {
    size_t pages = (min_payload + HDR + VMM_PAGE_SIZE - 1) / VMM_PAGE_SIZE;
    if (pages < HEAP_MIN_GROW) pages = HEAP_MIN_GROW;

    void *mem = vmm_alloc(pages, VMM_KERNEL_FLAGS | VMM_FLAG_NX);
    if (!mem) return -1;

    struct block *nb = mem;
    nb->size = pages * VMM_PAGE_SIZE - HDR;
    nb->magic = HEAP_MAGIC_FREE;
    nb->next = NULL;

    if (!heap_head) {
        heap_head = nb;
        return 0;
    }

    /* New memory is at a higher address than everything else: append */
    struct block *tail = heap_head;
    while (tail->next) tail = tail->next;
    tail->next = nb;

    /* Merge with a free, adjacent tail */
    if (tail->magic == HEAP_MAGIC_FREE && adjacent(tail)) {
        tail->size += HDR + nb->size;
        tail->next = nb->next;
        nb->magic = 0;
    }
    return 0;
}

void heap_init(void) {
    if (heap_ready) return;
    if (heap_grow(0) != 0) {
        kernel_panic("heap: cannot allocate initial heap");
    }
    heap_ready = 1;

    struct heap_stats st;
    heap_get_stats(&st);
    kprintf("Heap: initialized, %zu KB available\n", st.free / 1024);
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    if (!heap_ready) return NULL;
    size = align_up(size);
    if (size < HEAP_ALIGN) size = HEAP_ALIGN;

    for (int attempt = 0; attempt < 2; attempt++) {
        for (struct block *b = heap_head; b; b = b->next) {
            if (b->magic != HEAP_MAGIC_FREE || b->size < size) continue;

            /* Split if the remainder can hold a header plus a minimal payload */
            if (b->size >= size + HDR + HEAP_ALIGN) {
                struct block *rest = (struct block *)((uint8_t *)b + HDR + size);
                rest->size = b->size - size - HDR;
                rest->magic = HEAP_MAGIC_FREE;
                rest->next = b->next;
                b->next = rest;
                b->size = size;
            }
            b->magic = HEAP_MAGIC_USED;
            return payload(b);
        }
        if (attempt == 0 && heap_grow(size) != 0) return NULL;
    }
    return NULL;
}

void kfree(void *ptr) {
    if (!ptr) return;

    struct block *b = header(ptr);
    if (b->magic == HEAP_MAGIC_FREE) {
        kernel_panic("kfree: double free of %p", ptr);
    }
    if (b->magic != HEAP_MAGIC_USED) {
        kernel_panic("kfree: invalid pointer %p (bad header magic)", ptr);
    }
    b->magic = HEAP_MAGIC_FREE;

    /* Coalesce with the next block */
    if (b->next && b->next->magic == HEAP_MAGIC_FREE && adjacent(b)) {
        struct block *n = b->next;
        b->size += HDR + n->size;
        b->next = n->next;
        n->magic = 0;
    }

    /* Coalesce with the previous block (singly linked: find it) */
    struct block *prev = NULL;
    for (struct block *it = heap_head; it && it != b; it = it->next) prev = it;
    if (prev && prev->magic == HEAP_MAGIC_FREE && adjacent(prev)) {
        prev->size += HDR + b->size;
        prev->next = b->next;
        b->magic = 0;
    }
}

void *kcalloc(size_t nmemb, size_t size) {
    if (nmemb && size > (size_t)-1 / nmemb) return NULL;   /* overflow */
    size_t total = nmemb * size;
    void *p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    struct block *b = header(ptr);
    if (b->magic != HEAP_MAGIC_USED) {
        kernel_panic("krealloc: invalid pointer %p", ptr);
    }
    if (align_up(size) <= b->size) return ptr;   /* already big enough */

    void *np = kmalloc(size);
    if (!np) return NULL;
    memcpy(np, ptr, b->size);
    kfree(ptr);
    return np;
}

void heap_get_stats(struct heap_stats *out) {
    memset(out, 0, sizeof(*out));
    for (struct block *b = heap_head; b; b = b->next) {
        out->blocks++;
        out->total += b->size;
        if (b->magic == HEAP_MAGIC_USED) out->used += b->size;
        else out->free += b->size;
    }
}
