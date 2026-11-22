/* Virtual Memory Manager Implementation
 *
 * Limine hands the kernel a working 4-level page table (kernel image in the
 * higher half, all physical memory at the HHDM offset, framebuffer, ...).
 * The VMM adopts that PML4 and adds/removes mappings on top of it. It never
 * switches to a table that lacks the HHDM or the kernel stack.
 */

#include <kernel/mm/vmm.h>
#include <kernel/mm/pmm.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

/* Page table entry */
typedef uint64_t pte_t;

#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITE    (1ULL << 1)
#define PTE_USER     (1ULL << 2)
#define PTE_HUGE     (1ULL << 7)
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

/* Page table levels */
#define PT_ENTRIES 512

static uint64_t vmm_hhdm_offset = 0;
static pte_t *kernel_pml4 = NULL;

/* Physical -> virtual through the HHDM */
static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + vmm_hhdm_offset);
}

static inline void tlb_invalidate(uint64_t vaddr) {
    asm volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

/* Walk to the leaf (4 KiB) PTE for 'virt'.
 * With create != 0, missing intermediate tables are allocated.
 * Returns NULL if a table is missing (and !create), if allocation fails, or
 * if the address is covered by a huge page (no 4 KiB PTE exists there). */
static pte_t *vmm_walk(pte_t *pml4, uint64_t virt, int create, uint64_t flags) {
    pte_t *table = pml4;
    for (int level = 3; level >= 0; level--) {
        size_t idx = (virt >> (12 + level * 9)) & 0x1FF;

        if (level == 0) return &table[idx];

        pte_t entry = table[idx];
        if (!(entry & PTE_PRESENT)) {
            if (!create) return NULL;
            void *page = pmm_alloc(1);
            if (!page) return NULL;
            memset(phys_to_virt((uint64_t)page), 0, VMM_PAGE_SIZE);
            entry = (uint64_t)page | PTE_PRESENT | PTE_WRITE | (flags & PTE_USER);
            table[idx] = entry;
        } else if (entry & PTE_HUGE) {
            return NULL;   /* covered by a 1 GiB / 2 MiB page */
        } else if ((flags & PTE_USER) && !(entry & PTE_USER)) {
            entry |= PTE_USER;
            table[idx] = entry;
        }

        table = phys_to_virt(entry & PTE_ADDR_MASK);
    }
    return NULL;
}

void vmm_init(uint64_t hhdm_offset) {
    vmm_hhdm_offset = hhdm_offset;

    /* Adopt the page tables Limine set up */
    uint64_t cr3 = vmm_get_pml4();
    kernel_pml4 = phys_to_virt(cr3 & PTE_ADDR_MASK);

    kprintf("VMM: initialized, adopted PML4 phys 0x%lx, HHDM offset 0x%lx\n",
            cr3 & PTE_ADDR_MASK, vmm_hhdm_offset);
}

uint64_t vmm_get_hhdm(void) {
    return vmm_hhdm_offset;
}

int vmm_map(void *virt, void *phys, size_t count, uint64_t flags) {
    uint64_t vaddr = (uint64_t)virt;
    uint64_t paddr = (uint64_t)phys;

    if (vaddr & (VMM_PAGE_SIZE - 1)) return -1;
    if (paddr & (VMM_PAGE_SIZE - 1)) return -1;

    for (size_t i = 0; i < count; i++) {
        uint64_t va = vaddr + i * VMM_PAGE_SIZE;
        pte_t *pte = vmm_walk(kernel_pml4, va, 1, flags);
        if (!pte) return -1;
        *pte = (paddr + i * VMM_PAGE_SIZE) | flags | PTE_PRESENT;
        tlb_invalidate(va);
    }
    return 0;
}

int vmm_unmap(void *virt, size_t count) {
    uint64_t vaddr = (uint64_t)virt;

    if (vaddr & (VMM_PAGE_SIZE - 1)) return -1;

    for (size_t i = 0; i < count; i++) {
        uint64_t va = vaddr + i * VMM_PAGE_SIZE;
        pte_t *pte = vmm_walk(kernel_pml4, va, 0, 0);
        if (!pte) continue;
        *pte = 0;
        tlb_invalidate(va);
    }
    return 0;
}

void *vmm_virt_to_phys(void *virt) {
    uint64_t va = (uint64_t)virt;
    pte_t *table = kernel_pml4;

    for (int level = 3; level >= 0; level--) {
        size_t idx = (va >> (12 + level * 9)) & 0x1FF;
        pte_t entry = table[idx];
        if (!(entry & PTE_PRESENT)) return NULL;

        /* Huge pages: 1 GiB at level 2, 2 MiB at level 1 */
        if (level > 0 && (entry & PTE_HUGE)) {
            if (level == 3) return NULL;   /* PS must be 0 in the PML4 */
            uint64_t page_size = 1ULL << (12 + level * 9);
            uint64_t base = entry & PTE_ADDR_MASK & ~(page_size - 1);
            return (void *)(base | (va & (page_size - 1)));
        }

        if (level == 0) {
            return (void *)((entry & PTE_ADDR_MASK) | (va & (VMM_PAGE_SIZE - 1)));
        }
        table = phys_to_virt(entry & PTE_ADDR_MASK);
    }
    return NULL;
}

/* Bump allocator for kernel virtual space (never reused, which is fine for
 * the 1 GiB kernel heap window; vmm_free only returns the physical pages). */
void *vmm_alloc(size_t pages, uint64_t flags) {
    static uint64_t next_vaddr = VMM_KERNEL_HEAP_BASE;
    if (pages == 0) return NULL;

    uint64_t base = (next_vaddr + VMM_PAGE_SIZE - 1) & ~(uint64_t)(VMM_PAGE_SIZE - 1);

    for (size_t i = 0; i < pages; i++) {
        uint64_t va = base + i * VMM_PAGE_SIZE;
        void *phys = pmm_alloc(1);
        if (!phys || vmm_map((void *)va, phys, 1, flags)) {
            if (phys) pmm_free(phys, 1);
            /* Roll back the pages mapped so far */
            for (size_t j = 0; j < i; j++) {
                uint64_t rva = base + j * VMM_PAGE_SIZE;
                void *rphys = vmm_virt_to_phys((void *)rva);
                vmm_unmap((void *)rva, 1);
                if (rphys) pmm_free(rphys, 1);
            }
            return NULL;
        }
    }

    next_vaddr = base + pages * VMM_PAGE_SIZE;
    memset((void *)base, 0, pages * VMM_PAGE_SIZE);
    return (void *)base;
}

void vmm_free(void *virt, size_t pages) {
    uint64_t vaddr = (uint64_t)virt;
    for (size_t i = 0; i < pages; i++) {
        uint64_t va = vaddr + i * VMM_PAGE_SIZE;
        void *phys = vmm_virt_to_phys((void *)va);
        vmm_unmap((void *)va, 1);
        if (phys) pmm_free(phys, 1);
    }
}

void vmm_switch_context(uint64_t pml4_phys) {
    asm volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

uint64_t vmm_get_pml4(void) {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}
