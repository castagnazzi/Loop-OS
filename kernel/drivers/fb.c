/* Linear framebuffer driver and text console */

#include <kernel/drivers/fb.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/limine.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>
#include "font8x8.h"

/* ------------------------------------------------------------------ */
/* Framebuffer state */
static uint8_t *fb_base = NULL;
static uint32_t fb_w = 0, fb_h = 0, fb_pitch = 0;
static uint8_t r_size, r_shift, g_size, g_shift, b_size, b_shift;

int fb_init(struct limine_framebuffer *fb) {
    if (!fb || !fb->address) return -1;
    if (fb->bpp != 32) {
        kprintf("FB: unsupported bpp %d (need 32)\n", (int)fb->bpp);
        return -1;
    }

    fb_base = (uint8_t *)fb->address;
    fb_w = (uint32_t)fb->width;
    fb_h = (uint32_t)fb->height;
    fb_pitch = (uint32_t)fb->pitch;
    r_size = fb->red_mask_size;   r_shift = fb->red_mask_shift;
    g_size = fb->green_mask_size; g_shift = fb->green_mask_shift;
    b_size = fb->blue_mask_size;  b_shift = fb->blue_mask_shift;
    return 0;
}

int fb_ready(void) { return fb_base != NULL; }
uint32_t fb_width(void) { return fb_w; }
uint32_t fb_height(void) { return fb_h; }

static inline uint32_t scale_channel(uint8_t v, uint8_t size, uint8_t shift) {
    if (size == 0 || size > 8) size = 8;
    return ((uint32_t)(v >> (8 - size))) << shift;
}

uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return scale_channel(r, r_size, r_shift) |
           scale_channel(g, g_size, g_shift) |
           scale_channel(b, b_size, b_shift);
}

/* ------------------------------------------------------------------ */
/* Primitives */
static inline uint32_t *row_ptr(int y) {
    return (uint32_t *)(fb_base + (uint64_t)y * fb_pitch);
}

/* ------------------------------------------------------------------ */
/* Mouse cursor: an arrow drawn over the screen with the pixels underneath
 * saved, so moving it restores the background. Drawing primitives hide it
 * when they touch its area; all of this runs with interrupts disabled so
 * the mouse IRQ cannot interleave with a half-finished draw. */
#define CUR_W 12
#define CUR_H 19
static const char *cursor_shape[CUR_H] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.........X ",
    "X......XXXXX",
    "X...X..X    ",
    "X..X X..X   ",
    "X.X  X..X   ",
    "XX    X..X  ",
    "X     X..X  ",
    "       X..X ",
    "       XXX  ",
};
static uint32_t cursor_under[CUR_W * CUR_H];
static int cursor_x = 0, cursor_y = 0;
static int cursor_enabled = 0;   /* feature switched on */
static int cursor_visible = 0;   /* currently drawn on screen */

static void cursor_draw(void) {
    uint32_t black = fb_rgb(0, 0, 0), white = fb_rgb(255, 255, 255);
    for (int j = 0; j < CUR_H; j++) {
        for (int i = 0; i < CUR_W; i++) {
            int px = cursor_x + i, py = cursor_y + j;
            if (px < 0 || py < 0 || px >= (int)fb_w || py >= (int)fb_h) continue;
            cursor_under[j * CUR_W + i] = row_ptr(py)[px];
            char c = cursor_shape[j][i];
            if (c == 'X') row_ptr(py)[px] = black;
            else if (c == '.') row_ptr(py)[px] = white;
        }
    }
    cursor_visible = 1;
}

static void cursor_erase(void) {
    if (!cursor_visible) return;
    for (int j = 0; j < CUR_H; j++) {
        for (int i = 0; i < CUR_W; i++) {
            int px = cursor_x + i, py = cursor_y + j;
            if (px < 0 || py < 0 || px >= (int)fb_w || py >= (int)fb_h) continue;
            if (cursor_shape[j][i] != ' ') row_ptr(py)[px] = cursor_under[j * CUR_W + i];
        }
    }
    cursor_visible = 0;
}

static inline int cursor_overlaps(int x, int y, int w, int h) {
    return cursor_visible && x < cursor_x + CUR_W && x + w > cursor_x &&
           y < cursor_y + CUR_H && y + h > cursor_y;
}

void fb_cursor_enable(int x, int y) {
    if (!fb_base) return;
    uint64_t f = interrupts_save();
    cursor_erase();
    cursor_x = x; cursor_y = y;
    cursor_enabled = 1;
    cursor_draw();
    interrupts_restore(f);
}

void fb_cursor_move(int x, int y) {
    if (!cursor_enabled) return;
    uint64_t f = interrupts_save();
    cursor_erase();
    cursor_x = x; cursor_y = y;
    cursor_draw();
    interrupts_restore(f);
}

/* Hide/show around raw framebuffer writes done outside the primitives */
int fb_cursor_hide(void) {
    uint64_t f = interrupts_save();
    int was = cursor_visible;
    cursor_erase();
    interrupts_restore(f);
    return was;
}

void fb_cursor_show(void) {
    if (!cursor_enabled) return;
    uint64_t f = interrupts_save();
    if (!cursor_visible) cursor_draw();
    interrupts_restore(f);
}

void fb_putpixel(int x, int y, uint32_t color) {
    if (!fb_base || x < 0 || y < 0 || x >= (int)fb_w || y >= (int)fb_h) return;
    uint64_t f = interrupts_save();
    int redraw = cursor_overlaps(x, y, 1, 1);
    if (redraw) cursor_erase();
    row_ptr(y)[x] = color;
    if (redraw) cursor_draw();
    interrupts_restore(f);
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (!fb_base || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)fb_w) w = (int)fb_w - x;
    if (y + h > (int)fb_h) h = (int)fb_h - y;
    if (w <= 0 || h <= 0) return;

    uint64_t f = interrupts_save();
    int redraw = cursor_overlaps(x, y, w, h);
    if (redraw) cursor_erase();
    for (int j = 0; j < h; j++) {
        uint32_t *p = row_ptr(y + j) + x;
        for (int i = 0; i < w; i++) p[i] = color;
    }
    if (redraw) cursor_draw();
    interrupts_restore(f);
}

void fb_clear(uint32_t color) {
    fb_fill_rect(0, 0, (int)fb_w, (int)fb_h, color);
}

void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg, int scale) {
    if (!fb_base) return;
    if (scale < 1) scale = 1;
    unsigned char uc = (unsigned char)c;
    if (uc < FONT_FIRST || uc > FONT_LAST) uc = '?';
    const uint8_t *glyph = font8x8[uc - FONT_FIRST];

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            int on = (glyph[row] >> col) & 1;     /* bit 0 = leftmost pixel */
            uint32_t color = on ? fg : bg;
            if (color == FB_TRANSPARENT) continue;
            fb_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
        }
    }
}

void fb_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg, int scale) {
    if (scale < 1) scale = 1;
    for (; *s; s++) {
        fb_draw_char(x, y, *s, fg, bg, scale);
        x += 8 * scale;
    }
}

/* ------------------------------------------------------------------ */
/* Boot screen */
int fb_draw_boot_screen(void) {
    if (!fb_base) return 0;

    /* Vertical gradient: deep blue to dark teal */
    for (uint32_t y = 0; y < fb_h; y++) {
        uint8_t t = (uint8_t)((y * 255) / (fb_h ? fb_h : 1));
        uint32_t c = fb_rgb((uint8_t)(10 + t / 20), (uint8_t)(18 + t / 6), (uint8_t)(48 + t / 5));
        fb_fill_rect(0, (int)y, (int)fb_w, 1, c);
    }

    /* Title bar */
    const int bar_h = 56;
    fb_fill_rect(0, 0, (int)fb_w, bar_h, fb_rgb(24, 28, 44));
    fb_fill_rect(0, bar_h, (int)fb_w, 3, fb_rgb(80, 160, 255));

    /* Logo: four colored squares */
    fb_fill_rect(16, 14, 12, 12, fb_rgb(255, 90, 90));
    fb_fill_rect(30, 14, 12, 12, fb_rgb(90, 220, 120));
    fb_fill_rect(16, 28, 12, 12, fb_rgb(90, 160, 255));
    fb_fill_rect(30, 28, 12, 12, fb_rgb(255, 210, 80));

    fb_draw_string(60, 12, "Loop-OS", fb_rgb(240, 245, 255), FB_TRANSPARENT, 3);
    fb_draw_string(60, 38, "x86_64 kernel - boot log", fb_rgb(150, 170, 200), FB_TRANSPARENT, 1);

    return bar_h + 3;
}

/* ------------------------------------------------------------------ */
/* Text console */
static int con_x, con_y, con_w, con_h;     /* viewport in pixels */
static int con_scale = 1;
static int con_cols, con_rows;
static int con_cx, con_cy;                 /* cursor in cells */
static uint32_t con_fg, con_bg;
static int con_ready = 0;

void fbcon_init(int x, int y, int w, int h, int scale, uint32_t fg, uint32_t bg) {
    if (!fb_base) return;
    if (scale < 1) scale = 1;
    con_x = x; con_y = y; con_w = w; con_h = h;
    con_scale = scale;
    con_fg = fg; con_bg = bg;
    con_cols = w / (8 * scale);
    con_rows = h / (8 * scale);
    con_cx = con_cy = 0;
    if (con_cols < 1 || con_rows < 1) return;
    fb_fill_rect(con_x, con_y, con_w, con_h, con_bg);
    con_ready = 1;
}

void fbcon_clear(void) {
    if (!con_ready) return;
    fb_fill_rect(con_x, con_y, con_w, con_h, con_bg);
    con_cx = con_cy = 0;
}

static void con_scroll(void) {
    int cell = 8 * con_scale;
    int move_rows = (con_rows - 1) * cell;

    /* Raw copy below: keep the cursor (and its saved pixels) out of the way */
    uint64_t f = interrupts_save();
    int redraw = cursor_overlaps(con_x, con_y, con_cols * cell, con_rows * cell);
    if (redraw) cursor_erase();
    for (int j = 0; j < move_rows; j++) {
        uint32_t *dst = row_ptr(con_y + j) + con_x;
        uint32_t *src = row_ptr(con_y + j + cell) + con_x;
        memmove(dst, src, (size_t)con_cols * cell * sizeof(uint32_t));
    }
    if (redraw) cursor_draw();
    interrupts_restore(f);

    fb_fill_rect(con_x, con_y + move_rows, con_cols * cell, cell, con_bg);
}

static void con_newline(void) {
    con_cx = 0;
    if (++con_cy >= con_rows) {
        con_cy = con_rows - 1;
        con_scroll();
    }
}

void fbcon_putc(char c) {
    if (!con_ready) return;
    int cell = 8 * con_scale;

    switch (c) {
    case '\n':
        con_newline();
        return;
    case '\r':
        con_cx = 0;
        return;
    case '\t': {
        int next = (con_cx + 8) & ~7;
        while (con_cx < next && con_cx < con_cols) {
            fb_fill_rect(con_x + con_cx * cell, con_y + con_cy * cell, cell, cell, con_bg);
            con_cx++;
        }
        if (con_cx >= con_cols) con_newline();
        return;
    }
    case '\b':
        if (con_cx > 0) {
            con_cx--;
            fb_fill_rect(con_x + con_cx * cell, con_y + con_cy * cell, cell, cell, con_bg);
        }
        return;
    default:
        break;
    }

    if (con_cx >= con_cols) con_newline();
    fb_draw_char(con_x + con_cx * cell, con_y + con_cy * cell, c, con_fg, con_bg, con_scale);
    con_cx++;
}
