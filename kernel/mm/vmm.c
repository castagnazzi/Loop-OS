/* Virtual Memory Manager Implementation */

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
#define PTE_PWT      (1ULL << 3)
#define PTE_PCD      (1ULL << 4)
#define PTE_ACCESSED (1ULL << 5)
#define PTE_DIRTY    (1ULL << 6)
#define PTE_HUGE     (1ULL << 7)
#define PTE_GLOBAL   (1ULL << 8)
#define PTE_NX       (1ULL << 63)
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

/* Page table levels */
#define PT_ENTRIES 512

static uint64_t vmm_hhdm_offset = 0;
static pte_t *kernel_pml4 = NULL;

/* Convert virtual to physical for kernel addresses (using HHDM) */
static inline void *phys_to_virt(void *phys) {
    return (void *)((uint64_t)phys + vmm_hhdm_offset);
}

static inline void *virt_to_phys_ptr(void *virt) {
    return (void *)((uint64_t)virt - vmm_hhdm_offset);
}

/* Get page table entry for address, creating tables if needed */
static pte_t *vmm_walk(pte_t *pml4, uint64_t virt, int create, uint64_t flags) {
    pte_t *table = pml4;
    for (int level = 3; level >= 0; level--) {
        size_t idx = (virt >> (12 + level * 9)) & 0x1FF;
        pte_t entry = table[idx];
        
        if (!(entry & PTE_PRESENT)) {
            if (!create) return NULL;
            void *page = pmm_alloc(1);
            if (!page) return NULL;
            memset(phys_to_virt(page), 0, VMM_PAGE_SIZE);
            entry = (uint64_t)page | flags | PTE_PRESENT | PTE_WRITE;
            table[idx] = entry;
        }
        
        if (level == 0) return &table[idx];
        table = phys_to_virt((void *)(entry & PTE_ADDR_MASK));
    }
    return NULL;
}

void vmm_init(uint64_t hhdm_offset) {
    vmm_hhdm_offset = hhdm_offset;
    
    /* Allocate kernel PML4 */
    void *pml4_phys = pmm_alloc(1);
    if (!pml4_phys) kernel_panic("VMM: failed to allocate PML4");
    kernel_pml4 = phys_to_virt(pml4_phys);
    memset(kernel_pml4, 0, VMM_PAGE_SIZE);
    
    /* Identity map first 4MB (for early boot) */
    for (uint64_t addr = 0; addr < 4 * 1024 * 1024; addr += VMM_PAGE_SIZE) {
        vmm_map((void *)addr, (void *)addr, 1, VMM_KERNEL_FLAGS);
    }
    
    /* Map kernel higher-half */
    extern char __kernel_vma_start[], __kernel_vma_end[];
    uint64_t kernel_start = (uint64_t)__kernel_vma_start;
    uint64_t kernel_end = (uint64_t)__kernel_vma_end;
    for (uint64_t addr = kernel_start; addr < kernel_end; addr += VMM_PAGE_SIZE) {
        void *phys = (void *)(addr - VMM_KERNEL_BASE);
        vmm_map((void *)addr, phys, 1, VMM_KERNEL_FLAGS);
    }
    
    /* Set up HHDM mapping (physical -> virtual at hhdm_offset) */
    /* This is done lazily via page fault handler in real implementation */
    
    /* Load kernel PML4 */
    asm volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
    
    kprintf("VMM: initialized, PML4 at %p, HHDM offset 0x%lx\n", kernel_pml4, vmm_hhdm_offset);
}

int vmm_map(void *virt, void *phys, size_t count, uint64_t flags) {
    uint64_t vaddr = (uint64_t)virt;
    uint64_t paddr = (uint64_t)phys;
    
    if (vaddr & (VMM_PAGE_SIZE - 1)) return -1;
    if (paddr & (VMM_PAGE_SIZE - 1)) return -1;
    
    for (size_t i = 0; i < count; i++) {
        pte_t *pte = vmm_walk(kernel_pml4, vaddr + i * VMM_PAGE_SIZE, 1, flags);
        if (!pte) return -1;
        *pte = (paddr + i * VMM_PAGE_SIZE) | flags | PTE_PRESENT;
    }
    
    /* Flush TLB */
    asm volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
    return 0;
}

int vmm_unmap(void *virt, size_t count) {
    uint64_t vaddr = (uint64_t)virt;
    
    if (vaddr & (VMM_PAGE_SIZE - 1)) return -1;
    
    for (size_t i = 0; i < count; i++) {
        pte_t *pte = vmm_walk(kernel_pml4, vaddr + i * VMM_PAGE_SIZE, 0, 0);
        if (!pte) continue;
        *pte = 0;
        asm volatile("invlpg (%0)" : : "r"(vaddr + i * VMM_PAGE_SIZE) : "memory");
    }
    return 0;
}

void *vmm_virt_to_phys(void *virt) {
    pte_t *pte = vmm_walk(kernel_pml4, (uint64_t)virt, 0, 0);
    if (!pte || !(*pte & PTE_PRESENT)) return NULL;
    return (void *)((*pte & PTE_ADDR_MASK) | ((uint64_t)virt & (VMM_PAGE_SIZE - 1)));
}

void *vmm_alloc(size_t pages, uint64_t flags) {
    /* Simple bump allocator for kernel virtual space */
    static uint64_t next_vaddr = VMM_KERNEL_HEAP_BASE;
    uint64_t aligned = (next_vaddr + VMM_PAGE_SIZE - 1) & ~(VMM_PAGE_SIZE - 1);
    void *virt = (void *)aligned;
    
    for (size_t i = 0; i < pages; i++) {
        void *phys = pmm_alloc(1);
        if (!phys) {
            /* Rollback */
            for (size_t j = 0; j < i; j++) {
                pmm_free((void *)((uint64_t)phys - j * VMM_PAGE_SIZE), 1);
            }
            return NULL;
        }
        if (vmm_map((void *)(aligned + i * VMM_PAGE_SIZE), phys, 1, flags)) {
            /* Rollback */
            for (size_t j = 0; j <= i; j++) {
                pmm_free((void *)((uint64_t)phys - j * VMM_PAGE_SIZE), 1);
            }
            return NULL;
        }
    }
    
    next_vaddr = aligned + pages * VMM_PAGE_SIZE;
    return virt;
}

void vmm_free(void *virt, size_t pages) {
    uint64_t vaddr = (uint64_t)virt;
    for (size_t i = 0; i < pages; i++) {
        void *phys = vmm_virt_to_phys((void *)(vaddr + i * VMM_PAGE_SIZE));
        if (phys) pmm_free(phys, 1);
        vmm_unmap((void *)(vaddr + i * VMM_PAGE_SIZE), 1);
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