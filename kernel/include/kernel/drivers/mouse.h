/* PS/2 Mouse Driver */

#ifndef _KERNEL_DRIVERS_MOUSE_H
#define _KERNEL_DRIVERS_MOUSE_H

#include <stdint.h>

#define MOUSE_BTN_LEFT   (1 << 0)
#define MOUSE_BTN_RIGHT  (1 << 1)
#define MOUSE_BTN_MIDDLE (1 << 2)

/* Enable the PS/2 aux port and start streaming. 'max_x'/'max_y' are the
 * screen bounds the position is clamped to. Returns 0 on success. */
int mouse_init(int max_x, int max_y);

/* Current state */
int mouse_x(void);
int mouse_y(void);
uint8_t mouse_buttons(void);
int mouse_present(void);

/* Increments on every processed packet (cheap change detection) */
uint32_t mouse_serial(void);

/* IRQ12 handler */
void mouse_handler(void *frame);

#endif
