/* libgui implementation */

#include <gui.h>
#include "../../kernel/drivers/font8x8.h"

static inline long sys3(long n, long a, long b, long c) {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return ret;
}

int fd_avail(int fd) { return (int)sys3(SYS_FD_AVAIL, fd, 0, 0); }
long walltime(void) { return sys3(SYS_WALLTIME, 0, 0, 0); }

int gui_open(struct gwin *g, int w, int h, const char *title) {
    long id = sys3(SYS_WIN_CREATE, w, h, (long)title);
    if (id < 0) return -1;
    void *mem = sbrk((long)w * h * 4);
    if (mem == (void *)-1) return -1;
    g->id = (int)id;
    g->w = w;
    g->h = h;
    g->px = mem;
    memset(g->px, 0, (size_t)w * h * 4);
    return 0;
}

void gui_close(struct gwin *g) { sys3(SYS_WIN_DESTROY, g->id, 0, 0); }
void gui_present(struct gwin *g) { sys3(SYS_WIN_PRESENT, g->id, (long)g->px, 0); }

int gui_poll(struct gwin *g, struct win_event *ev) { return (int)sys3(SYS_WIN_EVENT, g->id, (long)ev, 0); }
int gui_wait(struct gwin *g, struct win_event *ev) { return (int)sys3(SYS_WIN_EVENT, g->id, (long)ev, 1); }

void gui_fill(struct gwin *g, int x, int y, int w, int h, uint32_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g->w) w = g->w - x;
    if (y + h > g->h) h = g->h - y;
    for (int j = 0; j < h; j++) {
        uint32_t *row = g->px + (size_t)(y + j) * g->w + x;
        for (int i = 0; i < w; i++) row[i] = color;
    }
}

void gui_rect(struct gwin *g, int x, int y, int w, int h, uint32_t color) {
    gui_fill(g, x, y, w, 1, color);
    gui_fill(g, x, y + h - 1, w, 1, color);
    gui_fill(g, x, y, 1, h, color);
    gui_fill(g, x + w - 1, y, 1, h, color);
}

void gui_char(struct gwin *g, int x, int y, char c, uint32_t color, int scale) {
    unsigned char uc = (unsigned char)c;
    if (uc < FONT_FIRST || uc > FONT_LAST) return;
    const uint8_t *gl = font8x8[uc - FONT_FIRST];
    for (int r = 0; r < 8; r++) {
        for (int col = 0; col < 8; col++) {
            if ((gl[r] >> col) & 1) gui_fill(g, x + col * scale, y + r * scale, scale, scale, color);
        }
    }
}

void gui_text(struct gwin *g, int x, int y, const char *s, uint32_t color, int scale) {
    for (; *s; s++, x += 8 * scale) gui_char(g, x, y, *s, color, scale);
}

void gui_button(struct gwin *g, int x, int y, int w, int h, const char *label, int pressed) {
    gui_fill(g, x, y, w, h, pressed ? RGB(70, 110, 190) : RGB(52, 60, 88));
    gui_rect(g, x, y, w, h, RGB(110, 130, 180));
    int tw = (int)strlen(label) * 8;
    gui_text(g, x + (w - tw) / 2, y + (h - 8) / 2, label, RGB(235, 240, 250), 1);
}

int gui_inside(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}
