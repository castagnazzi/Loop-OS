/* Physical Memory Manager Implementation */

#include <kernel/mm/pmm.h>
#include <kernel/limine.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

/* Bitmap-based page allocator */
static uint64_t *pmm_bitmap = NULL;
static size_t pmm_bitmap_size = 0;  /* in bits (pages) */
static uint64_t pmm_total_memory = 0;
static uint64_t pmm_used_memory = 0;
static uint64_t pmm_hhdm_offset = 0;

/* Find first zero bit in bitmap */
static size_t bitmap_find_free(uint64_t *bitmap, size_t bits, size_t count) {
    size_t words = bits / 64;
    for (size_t i = 0; i < words; i++) {
        if (bitmap[i] != ~0ULL) {
            /* Found a word with free bits */
            uint64_t word = ~bitmap[i];
            for (size_t bit = 0; bit < 64; bit++) {
                if (word & (1ULL << bit)) {
                    size_t start = i * 64 + bit;
                    if (start + count <= bits) {
                        /* Check if enough contiguous bits */
                        int ok = 1;
                        for (size_t c = 1; c < count; c++) {
                            size_t idx = (start + c) / 64;
                            size_t b = (start + c) % 64;
                            if (!(~bitmap[idx] & (1ULL << b))) { ok = 0; break; }
                        }
                        if (ok) return start;
                    }
                }
            }
        }
    }
    return (size_t)-1;
}

/* Set bits in bitmap */
static void bitmap_set(uint64_t *bitmap, size_t start, size_t count, int value) {
    for (size_t i = 0; i < count; i++) {
        size_t idx = (start + i) / 64;
        size_t bit = (start + i) % 64;
        if (value) bitmap[idx] |= (1ULL << bit);
        else bitmap[idx] &= ~(1ULL << bit);
    }
}

void pmm_init(struct limine_memmap_response *mm) {
    if (!mm) return;

    /* Find largest usable region for bitmap */
    uint64_t max_addr = 0;
    for (size_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE) {
            uint64_t end = e->base + e->length;
            if (end > max_addr) max_addr = end;
        }
    }

    pmm_bitmap_size = (max_addr + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    size_t bitmap_bytes = (pmm_bitmap_size + 7) / 8;
    bitmap_bytes = (bitmap_bytes + 7) & ~7;  /* align to 8 bytes */

    /* Find a usable region to place bitmap */
    for (size_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE && e->length >= bitmap_bytes) {
            pmm_bitmap = (uint64_t *)(e->base + pmm_hhdm_offset);
            /* Mark this region as used (for bitmap itself) */
            size_t bitmap_pages = (bitmap_bytes + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
            size_t bitmap_pfn = e->base / PMM_PAGE_SIZE;
            /* We'll mark it after zeroing */
            memset(pmm_bitmap, 0, bitmap_bytes);
            /* Mark bitmap pages as used */
            for (size_t p = 0; p < bitmap_pages; p++) {
                bitmap_set(pmm_bitmap, bitmap_pfn + p, 1, 1);
            }
            kprintf("PMM: bitmap at 0x%lx (%zu bytes, %zu pages)\n",
                    (uint64_t)pmm_bitmap, bitmap_bytes, bitmap_pages);
            break;
        }
    }

    if (!pmm_bitmap) {
        kernel_panic("PMM: no space for bitmap");
    }

    /* Mark all memory as used initially */
    for (size_t i = 0; i < pmm_bitmap_size / 64; i++) {
        pmm_bitmap[i] = ~0ULL;
    }

    /* Mark usable regions as free */
    for (size_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE) {
            size_t start_pfn = e->base / PMM_PAGE_SIZE;
            size_t page_count = (e->length + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
            bitmap_set(pmm_bitmap, start_pfn, page_count, 0);
            pmm_total_memory += e->length;
        }
    }

    /* Mark kernel and modules as used (handled by Limine memmap type) */
    /* Limine already marks KERNEL_AND_MODULES as non-usable */

    kprintf("PMM: initialized, total memory: %lu MB\n", pmm_total_memory / 1024 / 1024);
}

void *pmm_alloc(size_t count) {
    if (count == 0) return NULL;
    size_t pfn = bitmap_find_free(pmm_bitmap, pmm_bitmap_size, count);
    if (pfn == (size_t)-1) return NULL;
    bitmap_set(pmm_bitmap, pfn, count, 1);
    pmm_used_memory += count * PMM_PAGE_SIZE;
    return PFN_TO_ADDR(pfn);
}

void *pmm_alloc_aligned(size_t count, size_t alignment) {
    if (count == 0) return NULL;
    alignment /= PMM_PAGE_SIZE;
    size_t words = pmm_bitmap_size / 64;
    for (size_t i = 0; i < words; i++) {
        if (pmm_bitmap[i] != ~0ULL) {
            uint64_t word = ~pmm_bitmap[i];
            for (size_t bit = 0; bit < 64; bit++) {
                if (word & (1ULL << bit)) {
                    size_t start = i * 64 + bit;
                    /* Align up */
                    size_t aligned = (start + alignment - 1) & ~(alignment - 1);
                    if (aligned + count <= pmm_bitmap_size) {
                        int ok = 1;
                        for (size_t c = 0; c < count; c++) {
                            size_t idx = (aligned + c) / 64;
                            size_t b = (aligned + c) % 64;
                            if (pmm_bitmap[idx] & (1ULL << b)) { ok = 0; break; }
                        }
                        if (ok) {
                            bitmap_set(pmm_bitmap, aligned, count, 1);
                            pmm_used_memory += count * PMM_PAGE_SIZE;
                            return PFN_TO_ADDR(aligned);
                        }
                    }
                }
            }
        }
    }
    return NULL;
}

void *pmm_alloc_dma(size_t count) {
    /* Allocate below 4GB for DMA */
    const uint64_t DMA_LIMIT = 0x100000000ULL;  /* 4GB */
    size_t max_pfn = DMA_LIMIT / PMM_PAGE_SIZE;
    if (max_pfn > pmm_bitmap_size) max_pfn = pmm_bitmap_size;

    for (size_t i = 0; i < max_pfn / 64; i++) {
        if (pmm_bitmap[i] != ~0ULL) {
            uint64_t word = ~pmm_bitmap[i];
            for (size_t bit = 0; bit < 64; bit++) {
                if (word & (1ULL << bit)) {
                    size_t start = i * 64 + bit;
                    if (start + count <= max_pfn) {
                        int ok = 1;
                        for (size_t c = 1; c < count; c++) {
                            size_t idx = (start + c) / 64;
                            size_t b = (start + c) % 64;
                            if (pmm_bitmap[idx] & (1ULL << b)) { ok = 0; break; }
                        }
                        if (ok) {
                            bitmap_set(pmm_bitmap, start, count, 1);
                            pmm_used_memory += count * PMM_PAGE_SIZE;
                            return PFN_TO_ADDR(start);
                        }
                    }
                }
            }
        }
    }
    return NULL;
}

void pmm_free(void *addr, size_t count) {
    if (!addr || count == 0) return;
    uint64_t pfn = ADDR_TO_PFN(addr);
    if (pfn + count > pmm_bitmap_size) return;
    bitmap_set(pmm_bitmap, pfn, count, 0);
    if (pmm_used_memory >= count * PMM_PAGE_SIZE) {
        pmm_used_memory -= count * PMM_PAGE_SIZE;
    }
}

size_t pmm_total_pages(void) { return pmm_total_memory / PMM_PAGE_SIZE; }
size_t pmm_free_pages(void) { return (pmm_total_memory - pmm_used_memory) / PMM_PAGE_SIZE; }
size_t pmm_used_pages(void) { return pmm_used_memory / PMM_PAGE_SIZE; }