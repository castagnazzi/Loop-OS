/* ELF64 loader */

#include <kernel/elf.h>
#include <kernel/task.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

#define PAGE VMM_PAGE_SIZE

int elf_load(vmm_space_t *space, const uint8_t *img, size_t size,
             uint64_t *entry, uint64_t *brk_start) {
    if (size < sizeof(struct elf64_ehdr)) return -1;
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)img;

    if (eh->ident[0] != ELF_MAGIC0 || eh->ident[1] != 'E' ||
        eh->ident[2] != 'L' || eh->ident[3] != 'F') return -1;
    if (eh->ident[4] != 2) return -1;                 /* 64-bit */
    if (eh->type != ET_EXEC || eh->machine != EM_X86_64) return -1;
    if (eh->phentsize != sizeof(struct elf64_phdr) || eh->phnum == 0) return -1;
    if (eh->phoff + (uint64_t)eh->phnum * sizeof(struct elf64_phdr) > size) return -1;
    if (eh->entry >= USER_LIMIT) return -1;

    uint64_t end_of_image = 0;

    for (int i = 0; i < eh->phnum; i++) {
        const struct elf64_phdr *ph =
            (const struct elf64_phdr *)(img + eh->phoff + (uint64_t)i * sizeof(*ph));
        if (ph->type != PT_LOAD || ph->memsz == 0) continue;

        /* The segment must lie in user space, above the first page, and its
         * file data must be inside the image */
        if (ph->vaddr < PAGE || ph->vaddr + ph->memsz < ph->vaddr) return -1;
        if (ph->vaddr + ph->memsz >= USER_STACK_LOW) return -1;
        if (ph->filesz > ph->memsz) return -1;
        if (ph->offset + ph->filesz > size || ph->offset + ph->filesz < ph->offset) return -1;

        uint64_t flags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
        if (ph->flags & PF_W) flags |= VMM_FLAG_WRITE;
        if (!(ph->flags & PF_X)) flags |= VMM_FLAG_NX;

        uint64_t first = ph->vaddr & ~(uint64_t)(PAGE - 1);
        uint64_t last = (ph->vaddr + ph->memsz + PAGE - 1) & ~(uint64_t)(PAGE - 1);
        for (uint64_t va = first; va < last; va += PAGE) {
            void *existing = vmm_space_translate(space, va);
            if (existing) {
                /* Page shared with a neighbouring segment: keep the frame and
                 * loosen the permissions to the union (writable, executable) */
                vmm_space_map(space, va, (uint64_t)existing & ~(uint64_t)(PAGE - 1),
                              (flags | VMM_FLAG_WRITE) & ~VMM_FLAG_NX);
            } else if (vmm_space_alloc_page(space, va, flags) != 0) {
                return -1;
            }
        }

        if (ph->filesz &&
            vmm_space_copy_to(space, ph->vaddr, img + ph->offset, ph->filesz) != 0) {
            return -1;
        }

        if (ph->vaddr + ph->memsz > end_of_image) end_of_image = ph->vaddr + ph->memsz;
    }

    if (end_of_image == 0) return -1;
    *entry = eh->entry;
    *brk_start = (end_of_image + PAGE - 1) & ~(uint64_t)(PAGE - 1);
    return 0;
}
