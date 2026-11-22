/* ELF64 loader (static executables only) */

#ifndef _KERNEL_ELF_H
#define _KERNEL_ELF_H

#include <stddef.h>
#include <stdint.h>
#include <kernel/mm/vmm.h>

#define ELF_MAGIC0 0x7F
#define ET_EXEC    2
#define EM_X86_64  0x3E
#define PT_LOAD    1
#define PF_X       1
#define PF_W       2
#define PF_R       4

struct elf64_ehdr {
    uint8_t  ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed));

/* Map the PT_LOAD segments of the image at 'img' into 'space'.
 * On success returns 0 and sets *entry (start address) and *brk_start (first
 * page above the loaded segments, where the heap begins). */
int elf_load(vmm_space_t *space, const uint8_t *img, size_t size,
             uint64_t *entry, uint64_t *brk_start);

#endif
