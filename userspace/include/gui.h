/* libgui: draw into a window and receive its events.
 *
 * A window is a pixel buffer (0x00RRGGBB). Draw into it with the gui_*
 * functions, then call gui_present() to show the result. */

#ifndef GUI_H
#define GUI_H

#include <ulib.h>
#include <kernel/gui.h>

#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

struct gwin {
    int id, w, h;
    uint32_t *px;
};

/* Create a window with a w x h client area. Returns 0, or -1 on failure. */
int gui_open(struct gwin *g, int w, int h, const char *title);
void gui_close(struct gwin *g);
void gui_present(struct gwin *g);

/* Events: gui_poll returns 1 and fills 'ev' if one is waiting, 0 if none,
 * -1 if the window is gone. gui_wait blocks until one arrives. */
int gui_poll(struct gwin *g, struct win_event *ev);
int gui_wait(struct gwin *g, struct win_event *ev);

void gui_fill(struct gwin *g, int x, int y, int w, int h, uint32_t color);
void gui_rect(struct gwin *g, int x, int y, int w, int h, uint32_t color);   /* outline */
void gui_char(struct gwin *g, int x, int y, char c, uint32_t color, int scale);
void gui_text(struct gwin *g, int x, int y, const char *s, uint32_t color, int scale);
void gui_button(struct gwin *g, int x, int y, int w, int h, const char *label, int pressed);
int gui_inside(int px, int py, int x, int y, int w, int h);

#endif
