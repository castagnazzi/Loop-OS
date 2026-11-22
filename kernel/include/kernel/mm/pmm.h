/* Physical Memory Manager */

#ifndef _KERNEL_MM_PMM_H
#define _KERNEL_MM_PMM_H

#include <stdint.h>
#include <stddef.h>

/* Limine memory map entry */
struct limine_memmap_entry;
struct limine_memmap_response;

/* Initialize PMM from Limine memory map */
void pmm_init(struct limine_memmap_response *mm);

/* Allocate physical pages */
void *pmm_alloc(size_t count);           /* Allocate 'count' contiguous pages */
void *pmm_alloc_aligned(size_t count, size_t alignment);  /* With alignment */
void *pmm_alloc_dma(size_t count);       /* DMA-capable (< 4GB) */

/* Free physical pages */
void pmm_free(void *addr, size_t count);

/* Get memory stats */
size_t pmm_total_pages(void);
size_t pmm_free_pages(void);
size_t pmm_used_pages(void);

/* Page size */
#define PMM_PAGE_SIZE 4096

/* Page frame number helpers */
#define PFN_TO_ADDR(pfn) ((void *)((uint64_t)(pfn) * PMM_PAGE_SIZE))
#define ADDR_TO_PFN(addr) ((uint64_t)(addr) / PMM_PAGE_SIZE)

#endif