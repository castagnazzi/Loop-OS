/* PS/2 Mouse Driver
 *
 * Standard 3-byte packets:
 *   byte 0: Yovf Xovf Ysign Xsign 1 Mid Right Left
 *   byte 1: X movement (low 8 bits)   byte 2: Y movement (low 8 bits)
 * The cursor overlay is moved straight from the IRQ handler.
 */

#include <kernel/drivers/mouse.h>
#include <kernel/drivers/fb.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/stdio.h>
#include <stdint.h>

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

static int m_x, m_y, m_max_x, m_max_y;
static uint8_t m_buttons;
static int m_present = 0;
static volatile uint32_t m_serial = 0;

static uint8_t packet[3];
static int packet_idx = 0;

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Bounded waits so a machine without a mouse cannot hang the boot */
static int wait_write(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(PS2_STATUS) & 0x02)) return 0;
    }
    return -1;
}

static int wait_read(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS) & 0x01) return 0;
    }
    return -1;
}

/* Send one byte to the mouse itself (through the controller's 0xD4 prefix)
 * and wait for the 0xFA acknowledge. */
static int mouse_cmd(uint8_t cmd) {
    if (wait_write()) return -1;
    outb(PS2_CMD, 0xD4);
    if (wait_write()) return -1;
    outb(PS2_DATA, cmd);
    if (wait_read()) return -1;
    return inb(PS2_DATA) == 0xFA ? 0 : -1;
}

int mouse_init(int max_x, int max_y) {
    m_max_x = max_x > 0 ? max_x - 1 : 0;
    m_max_y = max_y > 0 ? max_y - 1 : 0;
    m_x = max_x / 2;
    m_y = max_y / 2;
    m_buttons = 0;
    packet_idx = 0;

    /* Enable the aux (mouse) port */
    if (wait_write()) goto fail;
    outb(PS2_CMD, 0xA8);

    /* Flush stale bytes */
    for (int i = 0; i < 16 && (inb(PS2_STATUS) & 0x01); i++) inb(PS2_DATA);

    /* Controller config: enable IRQ12 (bit 1), enable aux clock (clear bit 5) */
    if (wait_write()) goto fail;
    outb(PS2_CMD, 0x20);
    if (wait_read()) goto fail;
    uint8_t config = inb(PS2_DATA);
    config |= 0x02;
    config &= ~0x20;
    if (wait_write()) goto fail;
    outb(PS2_CMD, 0x60);
    if (wait_write()) goto fail;
    outb(PS2_DATA, config);

    /* Defaults, then start streaming movement packets */
    if (mouse_cmd(0xF6) || mouse_cmd(0xF4)) goto fail;

    irq_register_handler(12, (irq_handler_t)mouse_handler);
    pic_unmask(12);
    m_present = 1;
    kprintf("Mouse: PS/2 mouse enabled\n");
    return 0;

fail:
    kprintf("Mouse: no PS/2 mouse found\n");
    return -1;
}

void mouse_handler(void *frame) {
    (void)frame;
    /* Bit 5 of the status register marks data coming from the aux port */
    uint8_t status = inb(PS2_STATUS);
    if (!(status & 0x01)) return;
    uint8_t data = inb(PS2_DATA);
    if (!(status & 0x20)) return;   /* that byte belongs to the keyboard */

    /* Keep in sync: the first byte of a packet always has bit 3 set */
    if (packet_idx == 0 && !(data & 0x08)) return;

    packet[packet_idx++] = data;
    if (packet_idx < 3) return;
    packet_idx = 0;

    if (packet[0] & 0xC0) return;   /* overflow: drop the packet */

    int dx = (int)packet[1] - ((packet[0] & 0x10) ? 256 : 0);
    int dy = (int)packet[2] - ((packet[0] & 0x20) ? 256 : 0);

    m_x += dx;
    m_y -= dy;                      /* PS/2 Y grows upwards */
    if (m_x < 0) m_x = 0;
    if (m_y < 0) m_y = 0;
    if (m_x > m_max_x) m_x = m_max_x;
    if (m_y > m_max_y) m_y = m_max_y;
    m_buttons = packet[0] & 0x07;
    m_serial++;

    fb_cursor_move(m_x, m_y);
}

int mouse_x(void) { return m_x; }
int mouse_y(void) { return m_y; }
uint8_t mouse_buttons(void) { return m_buttons; }
int mouse_present(void) { return m_present; }
uint32_t mouse_serial(void) { return m_serial; }
