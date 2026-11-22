/* Title bar status line: uptime clock and mouse position */

#ifndef _KERNEL_STATUSBAR_H
#define _KERNEL_STATUSBAR_H

#include <stdint.h>

/* Called from the timer interrupt on every tick (100 Hz) */
void statusbar_tick(uint64_t tick);

/* Called by the mouse driver when the position or buttons changed */
void statusbar_mouse_changed(void);

#endif
