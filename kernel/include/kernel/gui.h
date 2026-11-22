/* Window system ABI (shared by the kernel and userspace) and the kernel
 * side of the desktop. Apps own a pixel buffer (0x00RRGGBB per pixel) and
 * hand it to the kernel with win_present(); the kernel composes windows,
 * taskbar and start menu, and delivers input as events. */

#ifndef _KERNEL_GUI_H
#define _KERNEL_GUI_H

#include <stdint.h>

#define WIN_EV_KEY   1    /* a = ASCII code */
#define WIN_EV_MOUSE 2    /* a = x, b = y (inside the window), c = button mask (1 = left) */
#define WIN_EV_CLOSE 3    /* the user pressed the close button */

struct win_event {
    int32_t type, a, b, c;
};

/* Ready the desktop (needs the framebuffer). Returns 0 on success. */
int wm_init(void);
/* The desktop main loop; never returns */
__attribute__((noreturn)) void wm_run(void);

/* Window syscalls: return an id (>= 1) or a negative errno */
int wm_create(int owner_pid, int w, int h, const char *title);
uint32_t *wm_surface(int id, int owner_pid, int *w, int *h);   /* NULL if invalid */
void wm_window_changed(int id);                                /* redraw after a present */
int wm_next_event(int id, int owner_pid, struct win_event *ev); /* 1 event, 0 none, -1 invalid */
int wm_destroy(int id, int owner_pid);
void wm_task_exit(int pid);                                    /* drop a dead task's windows */

#endif
