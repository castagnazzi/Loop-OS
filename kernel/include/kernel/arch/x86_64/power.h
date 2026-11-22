/* Shutdown and reboot */

#ifndef _KERNEL_ARCH_X86_64_POWER_H
#define _KERNEL_ARCH_X86_64_POWER_H

__attribute__((noreturn)) void power_off(void);
__attribute__((noreturn)) void power_reboot(void);

#endif
