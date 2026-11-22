/* Syscall handler (placeholder)
 *
 * Called from syscall_stub in interrupts.S. The real syscall table and
 * argument passing are not implemented yet (TODO: Fase 3+). Note that
 * syscall_stub also still needs GS base / kernel stack setup (swapgs,
 * %gs:0 and %gs:8) before it can be used safely.
 */

#include <kernel/stdio.h>

void syscall_handler(void) {
    kprintf("syscall: not implemented\n");
}
