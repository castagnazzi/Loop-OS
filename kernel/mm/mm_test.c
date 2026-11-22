/* Boot-time self tests for the memory subsystem (PMM, VMM, heap).
 * Results go to the serial log: one line per check plus a summary. */

#include <kernel/mm/mm_test.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vmm.h>
#include <kernel/mm/heap.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, name)                                         \
    do {                                                          \
        if (cond) {                                               \
            tests_passed++;                                       \
        } else {                                                  \
            tests_failed++;                                       \
            kprintf("  [FAIL] %s (line %d)\n", (name), __LINE__); \
        }                                                         \
    } while (0)

/* Small deterministic PRNG so the stress test is reproducible */
static uint32_t rng_state = 12345;
static uint32_t rng(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

static int fill_ok(const uint8_t *p, size_t n, uint8_t v) {
    for (size_t i = 0; i < n; i++) if (p[i] != v) return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
static void test_pmm(void) {
    kprintf("MM test: PMM\n");
    size_t free0 = pmm_free_pages();

    void *a = pmm_alloc(1);
    CHECK(a != NULL, "pmm_alloc(1) returns a page");
    CHECK(((uint64_t)a & (PMM_PAGE_SIZE - 1)) == 0, "page is 4K aligned");
    CHECK(a != NULL && (uint64_t)a >= PMM_PAGE_SIZE, "page 0 is never allocated");
    CHECK(pmm_free_pages() == free0 - 1, "free count drops by 1");

    void *b = pmm_alloc(4);
    CHECK(b != NULL, "pmm_alloc(4) returns pages");
    CHECK(b != a, "allocations do not overlap (distinct start)");
    CHECK(!(b >= a && (uint64_t)b < (uint64_t)a + PMM_PAGE_SIZE), "4-page block not inside the 1-page block");
    CHECK(pmm_free_pages() == free0 - 5, "free count drops by 5");

    pmm_free(b, 4);
    CHECK(pmm_free_pages() == free0 - 1, "free count recovers after pmm_free(4)");
    pmm_free(b, 4);   /* double free must not corrupt the counters */
    CHECK(pmm_free_pages() == free0 - 1, "double free does not change counters");

    void *c = pmm_alloc(4);
    CHECK(c == b, "freed block is reused (first fit)");
    pmm_free(c, 4);

    void *al = pmm_alloc_aligned(2, 2 * 1024 * 1024);
    CHECK(al != NULL, "pmm_alloc_aligned(2 MiB) succeeds");
    CHECK(al != NULL && ((uint64_t)al & (2 * 1024 * 1024 - 1)) == 0, "2 MiB alignment honoured");
    if (al) pmm_free(al, 2);

    void *dma = pmm_alloc_dma(2);
    CHECK(dma != NULL, "pmm_alloc_dma succeeds");
    CHECK(dma != NULL && (uint64_t)dma + 2 * PMM_PAGE_SIZE <= 0x100000000ULL, "DMA block is below 4 GiB");
    if (dma) pmm_free(dma, 2);

    CHECK(pmm_alloc(0) == NULL, "pmm_alloc(0) returns NULL");

    pmm_free(a, 1);
    CHECK(pmm_free_pages() == free0, "free count fully restored");
    CHECK(pmm_used_pages() + pmm_free_pages() == pmm_total_pages(), "used + free == total");
}

/* ------------------------------------------------------------------ */
static void test_vmm(void) {
    kprintf("MM test: VMM\n");
    uint64_t hhdm = vmm_get_hhdm();

    /* HHDM translation (Limine maps RAM with huge pages: exercises that path) */
    void *p = pmm_alloc(1);
    CHECK(p != NULL, "pmm page for HHDM test");
    if (p) {
        void *virt = (void *)((uint64_t)p + hhdm);
        void *back = vmm_virt_to_phys(virt);
        CHECK(back == p, "virt_to_phys(HHDM address) returns the physical page");
        *(volatile uint64_t *)virt = 0x1122334455667788ULL;
        CHECK(*(volatile uint64_t *)virt == 0x1122334455667788ULL, "HHDM page is readable/writable");
        pmm_free(p, 1);
    }

    /* Kernel image translation */
    extern char __kernel_vma_start[];
    void *kp = vmm_virt_to_phys(__kernel_vma_start);
    CHECK(kp != NULL, "kernel image is mapped");

    /* Allocate, write, verify */
    size_t pages = 4;
    uint8_t *mem = vmm_alloc(pages, VMM_KERNEL_FLAGS | VMM_FLAG_NX);
    CHECK(mem != NULL, "vmm_alloc(4)");
    if (!mem) return;
    CHECK(((uint64_t)mem & (VMM_PAGE_SIZE - 1)) == 0, "vmm_alloc result is page aligned");
    CHECK(fill_ok(mem, pages * VMM_PAGE_SIZE, 0), "vmm_alloc memory is zeroed");
    for (size_t i = 0; i < pages * VMM_PAGE_SIZE; i++) mem[i] = (uint8_t)(i ^ (i >> 8));
    int ok = 1;
    for (size_t i = 0; i < pages * VMM_PAGE_SIZE; i++) {
        if (mem[i] != (uint8_t)(i ^ (i >> 8))) { ok = 0; break; }
    }
    CHECK(ok, "pattern written across 4 pages reads back");

    void *ph = vmm_virt_to_phys(mem);
    CHECK(ph != NULL, "virt_to_phys of vmm_alloc memory");
    CHECK(vmm_virt_to_phys(mem + 0x123) == (void *)((uint64_t)ph + 0x123), "page offset preserved");

    /* Aliasing: map the same physical page at a second address */
    uint8_t *alias = (uint8_t *)VMM_MODULES_BASE;
    CHECK(vmm_map(alias, ph, 1, VMM_KERNEL_FLAGS | VMM_FLAG_NX) == 0, "vmm_map alias");
    mem[10] = 0x5A;
    CHECK(alias[10] == 0x5A, "alias sees writes made through the original mapping");
    CHECK(vmm_virt_to_phys(alias) == ph, "alias translates to the same physical page");

    /* Unmap */
    CHECK(vmm_unmap(alias, 1) == 0, "vmm_unmap alias");
    CHECK(vmm_virt_to_phys(alias) == NULL, "unmapped address no longer translates");

    /* Misuse is rejected */
    CHECK(vmm_map((void *)(VMM_MODULES_BASE + 1), ph, 1, VMM_KERNEL_FLAGS) != 0, "unaligned virtual address rejected");

    /* Free: physical pages come back (page-table pages stay allocated) */
    size_t free_before = pmm_free_pages();
    vmm_free(mem, pages);
    CHECK(pmm_free_pages() >= free_before + pages, "vmm_free returns the physical pages");
    CHECK(vmm_virt_to_phys(mem) == NULL, "freed range is unmapped");
}

/* ------------------------------------------------------------------ */
#define N_SLOTS 256

static void test_heap(void) {
    kprintf("MM test: heap\n");
    struct heap_stats s0, s1;
    heap_get_stats(&s0);
    CHECK(s0.used == 0, "heap starts with nothing allocated");

    CHECK(kmalloc(0) == NULL, "kmalloc(0) returns NULL");

    /* Basic sizes, alignment and non-overlap */
    size_t sizes[] = {1, 7, 16, 17, 100, 1000, 4096, 5000};
    uint8_t *ptrs[8];
    for (int i = 0; i < 8; i++) {
        ptrs[i] = kmalloc(sizes[i]);
        CHECK(ptrs[i] != NULL, "kmalloc basic size");
        if (!ptrs[i]) return;
        CHECK(((uint64_t)ptrs[i] & 15) == 0, "payload is 16-byte aligned");
        memset(ptrs[i], 0x10 + i, sizes[i]);
    }
    int intact = 1;
    for (int i = 0; i < 8; i++) {
        if (!fill_ok(ptrs[i], sizes[i], (uint8_t)(0x10 + i))) intact = 0;
    }
    CHECK(intact, "blocks do not overwrite each other");

    /* Free in a scrambled order, then check full coalescing */
    int order[8] = {3, 0, 7, 1, 5, 2, 6, 4};
    for (int i = 0; i < 8; i++) kfree(ptrs[order[i]]);
    heap_get_stats(&s1);
    CHECK(s1.used == 0, "all memory free after kfree");
    CHECK(s1.total == s0.total && s1.blocks == s0.blocks, "free blocks coalesce back to the original layout");

    /* kcalloc zeroes, and detects overflow */
    uint8_t *z = kcalloc(100, 8);
    CHECK(z != NULL && fill_ok(z, 800, 0), "kcalloc returns zeroed memory");
    CHECK(kcalloc((size_t)-1, 2) == NULL, "kcalloc rejects size overflow");
    kfree(z);

    /* krealloc keeps the contents when growing */
    uint8_t *r = kmalloc(50);
    memset(r, 0xAB, 50);
    r = krealloc(r, 20000);
    CHECK(r != NULL && fill_ok(r, 50, 0xAB), "krealloc preserves data when growing");
    void *kr = krealloc(NULL, 64);
    CHECK(kr != NULL, "krealloc(NULL) behaves like kmalloc");
    kfree(kr);
    kfree(r);

    /* Large allocation forces the heap to grow */
    heap_get_stats(&s0);
    /* Ask for more than the free space so the heap must grow, whatever its
     * current size (this test can be re-run from the shell) */
    size_t big_size = s0.free + 64 * 1024;
    uint8_t *big = kmalloc(big_size);
    CHECK(big != NULL, "allocation larger than the free space succeeds (heap grows)");
    if (big) {
        memset(big, 0xCD, big_size);
        CHECK(fill_ok(big, big_size, 0xCD), "large block fully usable");
        heap_get_stats(&s1);
        CHECK(s1.total > s0.total, "heap total grew");
        kfree(big);
    }

    /* Stress: random allocate/free with content verification */
    static uint8_t *slot[N_SLOTS];
    static size_t slot_size[N_SLOTS];
    static uint8_t slot_val[N_SLOTS];
    memset(slot, 0, sizeof(slot));
    int stress_ok = 1;
    for (int iter = 0; iter < 4000 && stress_ok; iter++) {
        int i = rng() % N_SLOTS;
        if (slot[i]) {
            if (!fill_ok(slot[i], slot_size[i], slot_val[i])) { stress_ok = 0; break; }
            kfree(slot[i]);
            slot[i] = NULL;
        } else {
            size_t sz = 1 + (rng() % 3000);
            slot[i] = kmalloc(sz);
            if (!slot[i]) { stress_ok = 0; break; }
            slot_size[i] = sz;
            slot_val[i] = (uint8_t)rng();
            memset(slot[i], slot_val[i], sz);
        }
    }
    CHECK(stress_ok, "stress: 4000 random alloc/free keep their contents");
    for (int i = 0; i < N_SLOTS; i++) {
        if (slot[i]) {
            if (!fill_ok(slot[i], slot_size[i], slot_val[i])) stress_ok = 0;
            kfree(slot[i]);
        }
    }
    CHECK(stress_ok, "stress: surviving blocks intact");
    heap_get_stats(&s1);
    CHECK(s1.used == 0, "stress: nothing leaked");
}

/* ------------------------------------------------------------------ */
int mm_selftest(void) {
    tests_passed = 0;
    tests_failed = 0;

    test_pmm();
    test_vmm();
    test_heap();

    kprintf("MM selftest: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed;
}
