/* Serial Port (UART 16550) Driver Implementation */

#include <kernel/arch/x86_64/serial.h>
#include <kernel/stdio.h>
#include <stdint.h>

/* COM1 port */
#define COM1 0x3F8

/* Register offsets */
#define UART_RBR  0  /* Receive Buffer Register (read) */
#define UART_THR  0  /* Transmit Holding Register (write) */
#define UART_IER  1  /* Interrupt Enable Register */
#define UART_IIR  2  /* Interrupt Identification Register (read) */
#define UART_FCR  2  /* FIFO Control Register (write) */
#define UART_LCR  3  /* Line Control Register */
#define UART_MCR  4  /* Modem Control Register */
#define UART_LSR  5  /* Line Status Register */
#define UART_MSR  6  /* Modem Status Register */
#define UART_SCR  7  /* Scratch Register */

/* LCR bits */
#define LCR_DLAB 0x80  /* Divisor Latch Access Bit */

/* LSR bits */
#define LSR_DR   0x01  /* Data Ready */
#define LSR_THRE 0x20  /* Transmitter Holding Register Empty */
#define LSR_TEMT 0x40  /* Transmitter Empty */

static int serial_initialized = 0;
static uint16_t serial_port = COM1;

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void serial_init(void) {
    if (serial_initialized) return;
    
    /* Disable interrupts */
    outb(serial_port + UART_IER, 0x00);
    
    /* Enable DLAB to set baud rate */
    outb(serial_port + UART_LCR, LCR_DLAB);
    
    /* Set divisor for 115200 baud (115200 = 1843200 / 16) */
    outb(serial_port + UART_THR, 0x01);  /* DLL = 1 */
    outb(serial_port + UART_IER, 0x00);  /* DLM = 0 */
    
    /* 8N1, disable DLAB */
    outb(serial_port + UART_LCR, 0x03);
    
    /* Enable FIFO, clear, 14-byte threshold */
    outb(serial_port + UART_FCR, 0xC7);
    
    /* Enable interrupts, RTS/DSR */
    outb(serial_port + UART_MCR, 0x0B);
    
    /* Enable interrupts */
    outb(serial_port + UART_IER, 0x01);  /* Enable received data interrupt */
    
    serial_initialized = 1;
}

void serial_init_higher_half(void) {
    /* Re-init after paging - ports don't change */
    serial_init();
}

void serial_write_char(char c) {
    if (c == '\n') serial_write_char('\r');
    
    /* Wait for THR empty */
    while ((inb(serial_port + UART_LSR) & LSR_THRE) == 0) {
        asm volatile("pause");
    }
    
    outb(serial_port + UART_THR, c);
}

void serial_write_string(const char *str) {
    while (*str) serial_write_char(*str++);
}

char serial_read_char(void) {
    /* Wait for data ready */
    while ((inb(serial_port + UART_LSR) & LSR_DR) == 0) {
        asm volatile("pause");
    }
    return inb(serial_port + UART_RBR);
}

int serial_has_data(void) {
    return inb(serial_port + UART_LSR) & LSR_DR;
}