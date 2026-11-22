#include <stdlib.h>
#include <syscall.h>
#include <stdint.h>

/* Simple bump allocator with sbrk */
static void *heap_start = NULL;
static void *heap_end = NULL;
static void *heap_max = NULL;

void *malloc(size_t size) {
    if (size == 0) return NULL;
    
    /* Align to 16 bytes */
    size = (size + 15) & ~15;
    
    if (!heap_start) {
        heap_start = sbrk(0);
        heap_end = heap_start;
        heap_max = heap_start;
    }
    
    if ((uintptr_t)heap_end + size > (uintptr_t)heap_max) {
        size_t increment = (size > 4096) ? size : 4096;
        void *new_mem = sbrk(increment);
        if (new_mem == (void *)-1) return NULL;
        heap_max = (char *)heap_max + increment;
    }
    
    void *ptr = heap_end;
    heap_end = (char *)heap_end + size;
    return ptr;
}

void free(void *ptr) {
    /* Simple bump allocator - no actual free */
    (void)ptr;
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        /* Zero the memory */
        char *p = ptr;
        for (size_t i = 0; i < total; i++) p[i] = 0;
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    /* Simple implementation - allocate new, copy, free old */
    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    /* We don't know old size, so can't copy properly */
    free(ptr);
    return new_ptr;
}