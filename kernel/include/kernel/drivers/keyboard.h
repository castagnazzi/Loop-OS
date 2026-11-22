/* PS/2 Keyboard Driver */

#ifndef _KERNEL_DRIVERS_KEYBOARD_H
#define _KERNEL_DRIVERS_KEYBOARD_H

#include <stdint.h>

/* Initialize keyboard */
void keyboard_init(void);

/* Keyboard handler (called from IRQ1) */
void keyboard_handler(void *frame);

/* Get key from buffer (non-blocking) */
int keyboard_getchar(void);

/* Check if key available */
int keyboard_has_key(void);

/* Keyboard state */
struct keyboard_state {
    uint8_t shift : 1;
    uint8_t ctrl : 1;
    uint8_t alt : 1;
    uint8_t caps : 1;
    uint8_t num : 1;
    uint8_t scroll : 1;
    uint8_t extended : 1;
    uint8_t released : 1;
};
extern struct keyboard_state kbd_state;

#endif