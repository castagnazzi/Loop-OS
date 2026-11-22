/* Kernel Standard I/O */

#ifndef _KERNEL_STDIO_H
#define _KERNEL_STDIO_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* Print to serial */
void serial_write_string(const char *str);
void serial_write_char(char c);

/* Formatted print */
int kprintf(const char *fmt, ...);
int kvprintf(const char *fmt, va_list ap);
int ksnprintf(char *buf, size_t size, const char *fmt, ...);
int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

/* Panic */
__attribute__((noreturn)) void kernel_panic(const char *fmt, ...);

#endif