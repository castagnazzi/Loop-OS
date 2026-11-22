/* Title bar status (uptime and mouse), redrawn from the timer tick so it
 * keeps running whichever task owns the CPU. */

#include <kernel/statusbar.h>
#include <kernel/drivers/fb.h>
#include <kernel/drivers/mouse.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

#define TICKS_PER_SEC 100
#define MOUSE_REFRESH_TICKS 5

static volatile int mouse_dirty = 0;
static volatile int enabled = 1;

void statusbar_enable(int on) { enabled = on; }

static void draw_uptime(uint64_t secs) {
    char buf[32];
    ksnprintf(buf, sizeof(buf), "uptime %02lu:%02lu:%02lu",
              (unsigned long)(secs / 3600), (unsigned long)((secs / 60) % 60),
              (unsigned long)(secs % 60));
    int w = (int)(strlen(buf) * 16);
    int x = (int)fb_width() - w - 24;
    fb_fill_rect(x - 8, 14, w + 16, 28, fb_rgb(24, 28, 44));
    fb_draw_string(x, 20, buf, fb_rgb(150, 220, 170), FB_TRANSPARENT, 2);
}

static void draw_mouse(void) {
    char buf[48];
    uint8_t b = mouse_buttons();
    ksnprintf(buf, sizeof(buf), "mouse %4d,%4d [%c%c%c]", mouse_x(), mouse_y(),
              (b & MOUSE_BTN_LEFT) ? 'L' : '-',
              (b & MOUSE_BTN_MIDDLE) ? 'M' : '-',
              (b & MOUSE_BTN_RIGHT) ? 'R' : '-');
    int w = (int)(strlen(buf) * 8);
    int x = (int)fb_width() - w - 24;
    fb_fill_rect(x - 4, 42, w + 8, 10, fb_rgb(24, 28, 44));
    fb_draw_string(x, 43, buf, fb_rgb(150, 170, 200), FB_TRANSPARENT, 1);
}

void statusbar_mouse_changed(void) {
    mouse_dirty = 1;
}

void statusbar_tick(uint64_t tick) {
    if (!enabled || !fb_ready()) return;
    if (tick % TICKS_PER_SEC == 0) draw_uptime(tick / TICKS_PER_SEC);
    if (mouse_dirty && tick % MOUSE_REFRESH_TICKS == 0) {
        mouse_dirty = 0;
        if (mouse_present()) draw_mouse();
    }
}
