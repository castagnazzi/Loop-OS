/* Serial Port (UART) Driver */

#ifndef _KERNEL_ARCH_X86_64_SERIAL_H
#define _KERNEL_ARCH_X86_64_SERIAL_H

#include <stdint.h>

/* Initialize serial port (COM1) */
void serial_init(void);

/* Re-initialize after paging enabled */
void serial_init_higher_half(void);

/* Write single character */
void serial_write_char(char c);

/* Write string */
void serial_write_string(const char *str);

/* Read character (blocking) */
char serial_read_char(void);

/* Check if data available */
int serial_has_data(void);

#endif