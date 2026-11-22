/* Minimal in-kernel shell (temporary: real shell arrives with userspace) */

#ifndef _KERNEL_KSHELL_H
#define _KERNEL_KSHELL_H

/* Interactive prompt on the framebuffer console + PS/2 keyboard.
 * Needs interrupts enabled. Never returns. */
__attribute__((noreturn)) void kshell_run(void);

#endif
