/* Linear framebuffer driver + text console */

#ifndef _KERNEL_DRIVERS_FB_H
#define _KERNEL_DRIVERS_FB_H

#include <stdint.h>

struct limine_framebuffer;

/* Pass as 'bg' to fb_draw_char/fb_draw_string to leave the background alone */
#define FB_TRANSPARENT ((uint32_t)0xFFFFFFFFu)

/* Initialize from the Limine framebuffer. Returns 0 on success
 * (only 32 bits per pixel is supported). */
int fb_init(struct limine_framebuffer *fb);
int fb_ready(void);

uint32_t fb_width(void);
uint32_t fb_height(void);

/* Pack an 8-bit-per-channel color into the framebuffer pixel format */
uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b);

/* Drawing primitives (all clip to the screen) */
void fb_putpixel(int x, int y, uint32_t color);
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void fb_clear(uint32_t color);
void fb_draw_glyph(int x, int y, char c, uint32_t fg, uint32_t bg, int size_px);  /* any size >= 8 */
void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg, int scale);      /* integer scale */
void fb_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg, int scale);

/* Mouse cursor overlay (pixels underneath are saved/restored) */
void fb_cursor_enable(int x, int y);
void fb_cursor_move(int x, int y);
int fb_cursor_hide(void);     /* returns 1 if it was visible */
void fb_cursor_show(void);

/* Simple boot screen: gradient background and a title bar.
 * Returns the y coordinate where free drawing space starts. */
int fb_draw_boot_screen(void);

/* Text console inside a rectangle of the screen. Each character cell is
 * 'cell_px' x 'cell_px' pixels (the 8x8 font stretched, e.g. 12). Handles \n \r \t \b, wraps lines and scrolls. */
void fbcon_init(int x, int y, int w, int h, int cell_px, uint32_t fg, uint32_t bg);
void fbcon_putc(char c);
void fbcon_clear(void);

#endif
