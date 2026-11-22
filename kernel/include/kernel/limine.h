/* Limine Boot Protocol Structures for Loop-OS */
/* Based on Limine specification v2.x */

#ifndef _KERNEL_LIMINE_H
#define _KERNEL_LIMINE_H

#include <stdint.h>
#include <stddef.h>

/* ---------------------------------------------------------------- */
/* Base Revision */
/* Base revision tag: the bootloader sets 'revision' to 0 if the requested
 * revision is supported. */
struct limine_base_revision_request {
    uint64_t id[2];
    uint64_t revision;
};

/* ---------------------------------------------------------------- */
/* Framebuffer */
struct limine_framebuffer {
    uint64_t address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t reserved[7];
};

struct limine_framebuffer_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_framebuffer_response *response;
};

struct limine_framebuffer_response {
    uint64_t revision;
    uint64_t framebuffer_count;
    struct limine_framebuffer **framebuffers;
};

/* ---------------------------------------------------------------- */
/* Memory Map */
struct limine_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

#define LIMINE_MEMMAP_USABLE              0
#define LIMINE_MEMMAP_RESERVED            1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE    2
#define LIMINE_MEMMAP_ACPI_NVS            3
#define LIMINE_MEMMAP_BAD_MEMORY          4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_KERNEL_AND_MODULES  6
#define LIMINE_MEMMAP_FRAMEBUFFER         7

struct limine_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_memmap_response *response;
};

struct limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    struct limine_memmap_entry **entries;
};

/* ---------------------------------------------------------------- */
/* Entry Point */
struct limine_entry_point_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_entry_point_response *response;
    void (*entry_point)(void);
};

struct limine_entry_point_response {
    uint64_t revision;
};

/* ---------------------------------------------------------------- */
/* Kernel File */
struct limine_kernel_file_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_kernel_file_response *response;
};

struct limine_kernel_file_response {
    uint64_t revision;
    char *kernel_file_path;
};

/* ---------------------------------------------------------------- */
/* Module (initrd) */
struct limine_module {
    char *path;
    char *cmdline;
    uint64_t start;
    uint64_t size;
};

struct limine_module_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_module_response *response;
    uint64_t module_count;
    struct limine_module **modules;
};

struct limine_module_response {
    uint64_t revision;
    uint64_t module_count;
    struct limine_module **modules;
};

/* ---------------------------------------------------------------- */
/* RSDP (ACPI) */
struct limine_rsdp_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_rsdp_response *response;
};

struct limine_rsdp_response {
    uint64_t revision;
    void *address;
};

/* ---------------------------------------------------------------- */
/* SMP */
struct limine_smp_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_smp_response *response;
    uint64_t flags;
};

#define LIMINE_SMP_X2APIC (1 << 0)

struct limine_smp_response {
    uint64_t revision;
    uint64_t flags;
    uint64_t cpu_count;
    struct limine_smp_cpu **cpus;
};

struct limine_smp_cpu {
    uint64_t processor_id;
    uint64_t lapic_id;
    uint64_t reserved;
};

/* ---------------------------------------------------------------- */
/* HHDM (Higher Half Direct Map) */
struct limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_hhdm_response *response;
};

struct limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};

/* ---------------------------------------------------------------- */
/* Terminal (text mode) */
struct limine_terminal {
    uint64_t columns;
    uint64_t rows;
    struct limine_terminal_callback *callback;
};

struct limine_terminal_callback {
    void (*term_write)(struct limine_terminal *, const char *, uint64_t);
    void (*term_callback)(struct limine_terminal *);
};

struct limine_terminal_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_terminal_response *response;
    uint64_t terminal_count;
    struct limine_terminal **terminals;
};

struct limine_terminal_response {
    uint64_t revision;
    uint64_t terminal_count;
    struct limine_terminal **terminals;
};

/* ---------------------------------------------------------------- */
/* 5-level paging */
struct limine_5_level_paging_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_5_level_paging_response *response;
};

struct limine_5_level_paging_response {
    uint64_t revision;
};

#endif /* _KERNEL_LIMINE_H */