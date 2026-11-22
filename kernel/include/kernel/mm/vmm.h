/* Virtual Memory Manager */

#ifndef _KERNEL_MM_VMM_H
#define _KERNEL_MM_VMM_H

#include <stdint.h>
#include <stddef.h>

/* Page flags */
#define VMM_FLAG_PRESENT    (1ULL << 0)
#define VMM_FLAG_WRITE      (1ULL << 1)
#define VMM_FLAG_USER       (1ULL << 2)
#define VMM_FLAG_WRITETHROUGH (1ULL << 3)
#define VMM_FLAG_NOCACHE    (1ULL << 4)
#define VMM_FLAG_ACCESSED   (1ULL << 5)
#define VMM_FLAG_DIRTY      (1ULL << 6)
#define VMM_FLAG_HUGE       (1ULL << 7)   /* 2MB page */
#define VMM_FLAG_GLOBAL     (1ULL << 8)
#define VMM_FLAG_NX         (1ULL << 63)

/* Default kernel page flags */
#define VMM_KERNEL_FLAGS (VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_GLOBAL)
#define VMM_USER_FLAGS   (VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER)

/* Initialize VMM with HHDM offset */
void vmm_init(uint64_t hhdm_offset);

/* Map/unmap pages */
int vmm_map(void *virt, void *phys, size_t count, uint64_t flags);
int vmm_unmap(void *virt, size_t count);

/* Get physical address for virtual */
void *vmm_virt_to_phys(void *virt);

/* Allocate/free virtual address ranges (kernel space) */
void *vmm_alloc(size_t pages, uint64_t flags);
void vmm_free(void *virt, size_t pages);

/* Switch page table (context switch) */
void vmm_switch_context(uint64_t pml4_phys);

/* Get current PML4 physical address */
uint64_t vmm_get_pml4(void);

/* HHDM offset (virtual = physical + offset for all RAM) */
uint64_t vmm_get_hhdm(void);

/* Per-process address spaces. Kernel mappings are shared by all of them;
 * the lower half (user addresses) is private to each space. */
typedef struct vmm_space vmm_space_t;

vmm_space_t *vmm_space_create(void);
void vmm_space_destroy(vmm_space_t *sp);            /* frees all user pages */
int vmm_space_map(vmm_space_t *sp, uint64_t virt, uint64_t phys, uint64_t flags);
int vmm_space_alloc_page(vmm_space_t *sp, uint64_t virt, uint64_t flags);  /* zeroed frame */
void *vmm_space_translate(vmm_space_t *sp, uint64_t virt);   /* physical or NULL */
int vmm_space_copy_to(vmm_space_t *sp, uint64_t virt, const void *src, size_t n);
int vmm_space_copy_from(vmm_space_t *sp, void *dst, uint64_t virt, size_t n);
void vmm_space_switch(vmm_space_t *sp);
void vmm_switch_kernel(void);                       /* back to the kernel tables */

/* Page size constants */
#define VMM_PAGE_SIZE 4096
#define VMM_HUGE_PAGE_SIZE (2 * 1024 * 1024)

/* Kernel virtual address space layout */
#define VMM_KERNEL_BASE       0xFFFFFFFF80000000ULL
#define VMM_KERNEL_HEAP_BASE  0xFFFFFFFFC0000000ULL
#define VMM_HHDM_BASE         0xFFFF800000000000ULL  /* Default, overridden by Limine */
#define VMM_MODULES_BASE      0xFFFFFE0000000000ULL
#define VMM_STACK_TOP         0xFFFFFFFFFFFFF000ULL

/* Check if address is in kernel space */
#define VMM_IS_KERNEL_ADDR(addr) ((uint64_t)(addr) >= VMM_KERNEL_BASE)

#endif