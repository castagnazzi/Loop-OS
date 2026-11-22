/* Physical Memory Manager Implementation
 *
 * Bitmap allocator: one bit per 4 KiB page frame, 1 = used, 0 = free.
 * The bitmap itself lives in a usable region and is accessed through the
 * HHDM (higher-half direct map) set up by Limine.
 */

#include <kernel/mm/pmm.h>
#include <kernel/limine.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

static uint64_t *pmm_bitmap = NULL;
static size_t pmm_bitmap_size = 0;      /* number of pages covered (bits) */
static uint64_t pmm_total_memory = 0;   /* usable bytes reported by Limine */
static uint64_t pmm_used_memory = 0;    /* bytes currently allocated */
static uint64_t pmm_hhdm_offset = 0;

static inline int bit_test(size_t pfn) {
    return (pmm_bitmap[pfn / 64] >> (pfn % 64)) & 1;
}

/* Set or clear 'count' bits starting at 'start' */
static void bitmap_set(size_t start, size_t count, int value) {
    for (size_t i = 0; i < count; i++) {
        size_t idx = (start + i) / 64;
        size_t bit = (start + i) % 64;
        if (value) pmm_bitmap[idx] |= (1ULL << bit);
        else pmm_bitmap[idx] &= ~(1ULL << bit);
    }
}

/* True if 'count' pages starting at 'start' are all free and inside [0, limit) */
static int range_free(size_t start, size_t count, size_t limit) {
    if (start + count > limit) return 0;
    for (size_t c = 0; c < count; c++) {
        if (bit_test(start + c)) return 0;
    }
    return 1;
}

/* First-fit search for 'count' free pages, start aligned to 'align' pages,
 * with the whole range below 'limit' pages. Returns (size_t)-1 if none. */
static size_t find_free(size_t count, size_t align, size_t limit) {
    if (align == 0) align = 1;
    if (limit > pmm_bitmap_size) limit = pmm_bitmap_size;

    /* Page 0 is never handed out so NULL stays an invalid address */
    for (size_t start = align > 1 ? align : 1; start + count <= limit; start += align) {
        /* Skip full words quickly when searching unaligned */
        if (align == 1 && (start % 64) == 0 && pmm_bitmap[start / 64] == ~0ULL) {
            start += 63;  /* loop adds 1 */
            continue;
        }
        if (range_free(start, count, limit)) return start;
    }
    return (size_t)-1;
}

void pmm_init(struct limine_memmap_response *mm, uint64_t hhdm_offset) {
    if (!mm) return;
    pmm_hhdm_offset = hhdm_offset;

    /* Highest usable address decides how many pages the bitmap must cover */
    uint64_t max_addr = 0;
    for (size_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE) {
            uint64_t end = e->base + e->length;
            if (end > max_addr) max_addr = end;
        }
    }

    pmm_bitmap_size = (max_addr + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    size_t bitmap_bytes = ((pmm_bitmap_size + 63) / 64) * 8;   /* whole words */
    size_t bitmap_pages = (bitmap_bytes + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;

    /* Place the bitmap at the start of the first usable region big enough
     * (never at page 0) */
    uint64_t bitmap_phys = 0;
    for (size_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        uint64_t base = e->base < PMM_PAGE_SIZE ? PMM_PAGE_SIZE : e->base;
        uint64_t avail = (e->base + e->length) - base;
        if (avail >= bitmap_pages * PMM_PAGE_SIZE) {
            bitmap_phys = base;
            break;
        }
    }
    if (!bitmap_phys) {
        kernel_panic("PMM: no space for bitmap (%zu bytes)", bitmap_bytes);
    }

    pmm_bitmap = (uint64_t *)(bitmap_phys + pmm_hhdm_offset);

    /* 1) Everything starts as used */
    memset(pmm_bitmap, 0xFF, bitmap_bytes);

    /* 2) Usable regions become free */
    for (size_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE) {
            size_t start_pfn = e->base / PMM_PAGE_SIZE;
            size_t page_count = e->length / PMM_PAGE_SIZE;
            bitmap_set(start_pfn, page_count, 0);
            pmm_total_memory += page_count * PMM_PAGE_SIZE;
        }
    }

    /* 3) Reserve page 0 and the pages holding the bitmap itself */
    bitmap_set(0, 1, 1);
    bitmap_set(bitmap_phys / PMM_PAGE_SIZE, bitmap_pages, 1);

    /* Bytes used = usable total minus what is still marked free */
    size_t free_pages = 0;
    for (size_t pfn = 0; pfn < pmm_bitmap_size; pfn++) {
        if (!bit_test(pfn)) free_pages++;
    }
    pmm_used_memory = pmm_total_memory - (uint64_t)free_pages * PMM_PAGE_SIZE;

    kprintf("PMM: bitmap at phys 0x%lx (%zu bytes, %zu pages)\n",
            bitmap_phys, bitmap_bytes, bitmap_pages);
    kprintf("PMM: initialized, usable %lu MB, free %lu MB\n",
            pmm_total_memory / 1024 / 1024,
            (uint64_t)free_pages * PMM_PAGE_SIZE / 1024 / 1024);
}

void *pmm_alloc(size_t count) {
    if (count == 0) return NULL;
    size_t pfn = find_free(count, 1, pmm_bitmap_size);
    if (pfn == (size_t)-1) return NULL;
    bitmap_set(pfn, count, 1);
    pmm_used_memory += count * PMM_PAGE_SIZE;
    return PFN_TO_ADDR(pfn);
}

void *pmm_alloc_aligned(size_t count, size_t alignment) {
    if (count == 0) return NULL;
    size_t align_pages = alignment / PMM_PAGE_SIZE;
    if (align_pages == 0) align_pages = 1;
    /* Alignment must be a power of two number of pages */
    if (align_pages & (align_pages - 1)) return NULL;
    size_t pfn = find_free(count, align_pages, pmm_bitmap_size);
    if (pfn == (size_t)-1) return NULL;
    bitmap_set(pfn, count, 1);
    pmm_used_memory += count * PMM_PAGE_SIZE;
    return PFN_TO_ADDR(pfn);
}

void *pmm_alloc_dma(size_t count) {
    /* Allocate below 4 GiB for DMA */
    const size_t limit = 0x100000000ULL / PMM_PAGE_SIZE;
    if (count == 0) return NULL;
    size_t pfn = find_free(count, 1, limit);
    if (pfn == (size_t)-1) return NULL;
    bitmap_set(pfn, count, 1);
    pmm_used_memory += count * PMM_PAGE_SIZE;
    return PFN_TO_ADDR(pfn);
}

void pmm_free(void *addr, size_t count) {
    if (!addr || count == 0) return;
    uint64_t pfn = ADDR_TO_PFN(addr);
    if (pfn + count > pmm_bitmap_size) return;

    /* Only account for pages that were really allocated (guards double free) */
    size_t freed = 0;
    for (size_t i = 0; i < count; i++) {
        if (bit_test(pfn + i)) {
            bitmap_set(pfn + i, 1, 0);
            freed++;
        }
    }
    uint64_t bytes = (uint64_t)freed * PMM_PAGE_SIZE;
    pmm_used_memory = pmm_used_memory >= bytes ? pmm_used_memory - bytes : 0;
}

size_t pmm_total_pages(void) { return pmm_total_memory / PMM_PAGE_SIZE; }
size_t pmm_free_pages(void) { return (pmm_total_memory - pmm_used_memory) / PMM_PAGE_SIZE; }
size_t pmm_used_pages(void) { return pmm_used_memory / PMM_PAGE_SIZE; }
