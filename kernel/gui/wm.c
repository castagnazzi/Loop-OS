/* Window manager / compositor.
 *
 * Runs as the kernel task. Every window is a pixel surface owned by a user
 * program. The desktop is composed into a back buffer, but only the damaged
 * rectangle is redrawn and copied to the screen. Input: the keyboard goes to
 * the focused window; mouse clicks focus, drag and close windows, drive the
 * taskbar and the start menu, and are forwarded to the window under them. */

#include <kernel/gui.h>
#include <kernel/task.h>
#include <kernel/drivers/fb.h>
#include <kernel/drivers/mouse.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/drivers/timer.h>
#include <kernel/drivers/rtc.h>
#include <kernel/fs/vfs.h>
#include <kernel/mm/heap.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/power.h>
#include <kernel/net/net.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include "../drivers/font8x8.h"
#include "../drivers/font8x16.h"
#include "../drivers/logo.h"

#define MAX_WINDOWS 16
#define EVQ_LEN 32
#define TITLE_H 22
#define TASKBAR_H 36
#define MENU_ITEM_H 30
#define MENU_W 230
#define MAX_MENU 12
#define START_X 4
#define START_W 104

#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

#define C_TASKBAR   RGB(24, 28, 44)
#define C_ACCENT    RGB(80, 160, 255)
#define C_TEXT      RGB(235, 240, 250)
#define C_DIM       RGB(150, 170, 200)
#define C_TITLE_ON  RGB(40, 88, 168)
#define C_TITLE_OFF RGB(58, 62, 80)
#define C_BORDER    RGB(14, 18, 30)
#define C_MENU_BG   RGB(30, 36, 56)
#define C_MENU_HOT  RGB(52, 104, 190)
#define C_BTN       RGB(44, 52, 78)
#define C_BTN_HOT   RGB(70, 90, 140)

struct window {
    int used, id, owner;
    int x, y, w, h;                 /* outer top-left; w x h is the client area */
    int minimized;
    char title[32];
    uint32_t *px;
    struct win_event q[EVQ_LEN];
    int qhead, qcount;
};

struct menu_item {
    char label[32];
    char cmd[96];
    char icon[12];
};

static struct window wins[MAX_WINDOWS];
static int zorder[MAX_WINDOWS];     /* window slots, back to front */
static int zcount = 0;
static int next_id = 1;
static int focused = -1;            /* slot, or -1 */

static uint32_t *bb = NULL;         /* back buffer */
static int sw = 0, sh = 0;
static int clip_x0, clip_y0, clip_x1, clip_y1;     /* current clip, exclusive end */

/* damaged area (bounding box), empty when dx1 <= dx0 */
static int dx0 = 0, dy0 = 0, dx1 = 0, dy1 = 0;

/* Wallpaper picture loaded from /etc/wallpaper.lwp ("LWP1", w, h, ARGB pixels) */
static const uint32_t *wp_px = NULL;
static int wp_w = 0, wp_h = 0;

static struct menu_item menu[MAX_MENU];
static int menu_count = 0;
static int menu_open = 0;
static int menu_hot = -1;

static int drag_slot = -1, drag_dx = 0, drag_dy = 0;
static int capture_slot = -1;       /* window that got the left press */
static int prev_x = -1, prev_y = -1, prev_btn = 0;
static int clock_minute = -1;
static struct rtc_time now_time;

/* ------------------------------------------------------------------ */
/* Damage and clipping */
static void damage(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    if (w <= 0 || h <= 0) return;
    if (dx1 <= dx0) { dx0 = x; dy0 = y; dx1 = x + w; dy1 = y + h; return; }
    if (x < dx0) dx0 = x;
    if (y < dy0) dy0 = y;
    if (x + w > dx1) dx1 = x + w;
    if (y + h > dy1) dy1 = y + h;
}

static void damage_window(struct window *w) {
    damage(w->x - 4, w->y - 3, w->w + 2 + 14, w->h + TITLE_H + 1 + 14);     /* includes the shadow */
}

/* ------------------------------------------------------------------ */
/* Drawing into the back buffer (clipped to the damaged area) */
static void fill(int x, int y, int w, int h, uint32_t c) {
    int x0 = x < clip_x0 ? clip_x0 : x, y0 = y < clip_y0 ? clip_y0 : y;
    int x1 = x + w > clip_x1 ? clip_x1 : x + w, y1 = y + h > clip_y1 ? clip_y1 : y + h;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = bb + (size_t)j * sw;
        for (int i = x0; i < x1; i++) row[i] = c;
    }
}

static void glyph(int x, int y, char ch, uint32_t c, int scale) {
    unsigned char uc = (unsigned char)ch;
    if (uc < FONT_FIRST || uc > FONT_LAST) uc = '?';
    const uint8_t *g = font8x8[uc - FONT_FIRST];
    for (int r = 0; r < 8; r++) {
        for (int col = 0; col < 8; col++) {
            if ((g[r] >> col) & 1) fill(x + col * scale, y + r * scale, scale, scale, c);
        }
    }
}

static void text(int x, int y, const char *s, uint32_t c, int scale) {
    for (; *s; s++, x += 8 * scale) {
        if (x + 8 * scale <= clip_x0 || x >= clip_x1) continue;
        glyph(x, y, *s, c, scale);
    }
}

/* Readable 8x16 text (Latin-1); scale n draws 8n x 16n characters */
static void text16s(int x, int y, const char *s, uint32_t c, int scale) {
    for (; *s; s++, x += 8 * scale) {
        if (x + 8 * scale <= clip_x0 || x >= clip_x1) continue;
        const uint8_t *g = font8x16[(unsigned char)*s];
        for (int r = 0; r < 16; r++) {
            if (!g[r]) continue;
            for (int col = 0; col < 8; col++) {
                if (!((g[r] >> col) & 1)) continue;
                if (scale == 1) {
                    int px = x + col, py = y + r;
                    if (px >= clip_x0 && px < clip_x1 && py >= clip_y0 && py < clip_y1) bb[(size_t)py * sw + px] = c;
                } else {
                    fill(x + col * scale, y + r * scale, scale, scale, c);
                }
            }
        }
    }
}

static void text16(int x, int y, const char *s, uint32_t c) { text16s(x, y, s, c, 1); }

static uint32_t blend(uint32_t bg, uint32_t fg, unsigned a) {
    unsigned r = (((bg >> 16) & 0xFF) * (255 - a) + ((fg >> 16) & 0xFF) * a) / 255;
    unsigned g = (((bg >> 8) & 0xFF) * (255 - a) + ((fg >> 8) & 0xFF) * a) / 255;
    unsigned b = ((bg & 0xFF) * (255 - a) + (fg & 0xFF) * a) / 255;
    return RGB(r, g, b);
}

/* Logo scaled by an integer factor; 'opacity' (0..255) scales its alpha */
static void logo(int x, int y, int scale, unsigned opacity) {
    for (int j = 0; j < LOGO_H * scale; j++) {
        int py = y + j;
        if (py < clip_y0 || py >= clip_y1) continue;
        for (int i = 0; i < LOGO_W * scale; i++) {
            int px = x + i;
            if (px < clip_x0 || px >= clip_x1) continue;
            uint32_t p = logo_argb[(j / scale) * LOGO_W + (i / scale)];
            unsigned a = (p >> 24) * opacity / 255;
            if (!a) continue;
            uint32_t *d = &bb[(size_t)py * sw + px];
            *d = blend(*d, p & 0xFFFFFF, a);
        }
    }
}

/* Small logo for the start button: nearest-neighbour to 'h' pixels tall */
static void logo_small(int x, int y, int h) {
    int w = LOGO_W * h / LOGO_H;
    for (int j = 0; j < h; j++) {
        int py = y + j;
        if (py < clip_y0 || py >= clip_y1) continue;
        for (int i = 0; i < w; i++) {
            int px = x + i;
            if (px < clip_x0 || px >= clip_x1) continue;
            uint32_t p = logo_argb[(j * LOGO_H / h) * LOGO_W + (i * LOGO_H / h)];
            unsigned a = p >> 24;
            if (!a) continue;
            uint32_t *d = &bb[(size_t)py * sw + px];
            *d = blend(*d, p & 0xFFFFFF, a);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Menu (/etc/menu: one "Label|command arg" per line) */
static void load_menu(void) {
    menu_count = 0;
    struct vnode *n = vfs_lookup("/etc/menu");
    char buf[1024];
    long len = n ? vfs_read(n, 0, buf, sizeof(buf) - 1) : 0;
    if (len < 0) len = 0;
    buf[len] = '\0';

    char *p = buf;
    while (*p && menu_count < MAX_MENU) {
        char *eol = p;
        while (*eol && *eol != '\n') eol++;
        char *next = *eol ? eol + 1 : eol;
        *eol = '\0';
        char *bar = strchr(p, '|');
        if (bar && p[0] != '#') {
            *bar = '\0';
            strncpy(menu[menu_count].label, p, sizeof(menu[0].label) - 1);
            menu[menu_count].label[sizeof(menu[0].label) - 1] = '\0';
            menu[menu_count].icon[0] = '\0';
            char *bar2 = strchr(bar + 1, '|');
            if (bar2) {
                *bar2 = '\0';
                strncpy(menu[menu_count].icon, bar2 + 1, sizeof(menu[0].icon) - 1);
                menu[menu_count].icon[sizeof(menu[0].icon) - 1] = '\0';
            }
            strncpy(menu[menu_count].cmd, bar + 1, sizeof(menu[0].cmd) - 1);
            menu[menu_count].cmd[sizeof(menu[0].cmd) - 1] = '\0';
            menu_count++;
        }
        p = next;
    }
    if (menu_count == 0) {
        strcpy(menu[0].label, "Terminal");
        strcpy(menu[0].cmd, "/bin/term");
        menu_count = 1;
    }
}

static int menu_height(void) { return menu_count * MENU_ITEM_H + 8; }
static int menu_y(void) { return sh - TASKBAR_H - menu_height(); }

static void launch(const char *cmd) {
    if (!strcmp(cmd, "@reboot")) power_reboot();
    if (!strcmp(cmd, "@poweroff")) power_off();
    static char buf[96];
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    const char *argv[4];
    int argc = 0;
    char *p = buf;
    while (*p && argc < 3) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    if (argc == 0) return;
    argv[argc] = NULL;
    int r = task_spawn(argv[0], argv, argc);
    if (r < 0) kprintf("wm: cannot start %s (error %d)\n", argv[0], -r);
}

/* ------------------------------------------------------------------ */
/* Windows */
static struct window *by_slot(int slot) {
    return slot >= 0 && slot < MAX_WINDOWS && wins[slot].used ? &wins[slot] : NULL;
}

static int slot_of_id(int id, int owner_pid) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wins[i].used && wins[i].id == id && (owner_pid < 0 || wins[i].owner == owner_pid)) return i;
    }
    return -1;
}

static void raise_window(int slot) {
    int pos = -1;
    for (int i = 0; i < zcount; i++) if (zorder[i] == slot) pos = i;
    if (pos < 0) return;
    for (int i = pos; i < zcount - 1; i++) zorder[i] = zorder[i + 1];
    zorder[zcount - 1] = slot;
}

static void focus_window(int slot) {
    if (slot == focused) return;
    if (by_slot(focused)) damage_window(&wins[focused]);
    focused = slot;
    if (by_slot(slot)) {
        raise_window(slot);
        damage_window(&wins[slot]);
    }
}

/* Topmost visible window containing the point */
static int window_at(int x, int y) {
    for (int i = zcount - 1; i >= 0; i--) {
        struct window *w = &wins[zorder[i]];
        if (w->minimized) continue;
        if (x >= w->x && x < w->x + w->w + 2 && y >= w->y && y < w->y + w->h + TITLE_H + 1) return zorder[i];
    }
    return -1;
}

static void push_event(struct window *w, int type, int a, int b, int c) {
    /* Mouse moves replace a pending move instead of flooding the queue */
    if (type == WIN_EV_MOUSE && w->qcount > 0) {
        struct win_event *last = &w->q[(w->qhead + w->qcount - 1) % EVQ_LEN];
        if (last->type == WIN_EV_MOUSE && last->c == c) {
            last->a = a; last->b = b;
            return;
        }
    }
    if (w->qcount == EVQ_LEN) return;
    struct win_event *e = &w->q[(w->qhead + w->qcount) % EVQ_LEN];
    e->type = type; e->a = a; e->b = b; e->c = c;
    w->qcount++;
}

static void remove_window(int slot) {
    struct window *w = &wins[slot];
    damage_window(w);
    if (w->px) kfree(w->px);
    w->used = 0;
    w->px = NULL;
    int pos = 0;
    for (int i = 0; i < zcount; i++) if (zorder[i] != slot) zorder[pos++] = zorder[i];
    zcount = pos;
    if (focused == slot) {
        focused = -1;
        for (int i = zcount - 1; i >= 0; i--) {
            if (!wins[zorder[i]].minimized) { focused = zorder[i]; break; }
        }
        if (focused >= 0) damage_window(&wins[focused]);
    }
    if (capture_slot == slot) capture_slot = -1;
    if (drag_slot == slot) drag_slot = -1;
}

int wm_create(int owner_pid, int w, int h, const char *title) {
    if (!bb) return -38;
    if (w < 64 || h < 32 || w > sw - 2 || h > sh - TASKBAR_H - TITLE_H - 1) return -22;
    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) if (!wins[i].used) { slot = i; break; }
    if (slot < 0) return -11;

    uint64_t f = interrupts_save();
    struct window *win = &wins[slot];
    memset(win, 0, sizeof(*win));
    win->px = kmalloc((size_t)w * h * 4);
    if (!win->px) { interrupts_restore(f); return -12; }
    memset(win->px, 0x20, (size_t)w * h * 4);
    win->used = 1;
    win->id = next_id++;
    win->owner = owner_pid;
    win->w = w; win->h = h;
    strncpy(win->title, title, sizeof(win->title) - 1);

    int n = zcount % 8;
    win->x = 70 + n * 30;
    win->y = 30 + n * 30;
    if (win->x + w + 2 > sw) win->x = sw - w - 2;
    if (win->y + h + TITLE_H + 1 > sh - TASKBAR_H) win->y = sh - TASKBAR_H - h - TITLE_H - 1;
    if (win->x < 0) win->x = 0;
    if (win->y < 0) win->y = 0;

    zorder[zcount++] = slot;
    focus_window(slot);
    damage_window(win);
    damage(0, sh - TASKBAR_H, sw, TASKBAR_H);
    interrupts_restore(f);
    return win->id;
}

uint32_t *wm_surface(int id, int owner_pid, int *w, int *h) {
    int s = slot_of_id(id, owner_pid);
    if (s < 0) return NULL;
    *w = wins[s].w; *h = wins[s].h;
    return wins[s].px;
}

void wm_window_changed(int id) {
    int s = slot_of_id(id, -1);
    if (s < 0 || wins[s].minimized) return;
    struct window *w = &wins[s];
    damage(w->x + 1, w->y + TITLE_H, w->w, w->h);
}

int wm_next_event(int id, int owner_pid, struct win_event *ev) {
    int s = slot_of_id(id, owner_pid);
    if (s < 0) return -1;
    struct window *w = &wins[s];
    if (w->qcount == 0) return 0;
    *ev = w->q[w->qhead];
    w->qhead = (w->qhead + 1) % EVQ_LEN;
    w->qcount--;
    return 1;
}

int wm_destroy(int id, int owner_pid) {
    int s = slot_of_id(id, owner_pid);
    if (s < 0) return -9;
    uint64_t f = interrupts_save();
    remove_window(s);
    damage(0, sh - TASKBAR_H, sw, TASKBAR_H);
    interrupts_restore(f);
    return 0;
}

void wm_task_exit(int pid) {
    if (!bb) return;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wins[i].used && wins[i].owner == pid) remove_window(i);
    }
    damage(0, sh - TASKBAR_H, sw, TASKBAR_H);
}

/* ------------------------------------------------------------------ */
/* Composition */
/* Picture blended over the back buffer with a fixed opacity */
static void picture(int x, int y, const uint32_t *pix, int w, int h, unsigned opacity) {
    for (int j = 0; j < h; j++) {
        int py = y + j;
        if (py < clip_y0 || py >= clip_y1) continue;
        for (int i = 0; i < w; i++) {
            int px = x + i;
            if (px < clip_x0 || px >= clip_x1) continue;
            uint32_t p = pix[j * w + i];
            unsigned a = (p >> 24) * opacity / 255;
            if (!a) continue;
            uint32_t *d = &bb[(size_t)py * sw + px];
            *d = blend(*d, p & 0xFFFFFF, a);
        }
    }
}

/* Background gradient (top colour -> bottom colour). Ordered dithering hides
 * the banding that 8 bits per channel would otherwise show on such a gentle ramp. */
#define WALL_TOP_R 10
#define WALL_TOP_G 18
#define WALL_TOP_B 48
#define WALL_BOT_R 24
#define WALL_BOT_G 62
#define WALL_BOT_B 104

static void draw_wallpaper(void) {
    static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    int x0 = clip_x0 < 0 ? 0 : clip_x0, x1 = clip_x1 > sw ? sw : clip_x1;
    int y0 = clip_y0 < 0 ? 0 : clip_y0, y1 = clip_y1 > sh ? sh : clip_y1;
    for (int y = y0; y < y1; y++) {
        int r = (WALL_TOP_R << 8) + ((WALL_BOT_R - WALL_TOP_R) * y << 8) / sh;
        int g = (WALL_TOP_G << 8) + ((WALL_BOT_G - WALL_TOP_G) * y << 8) / sh;
        int b = (WALL_TOP_B << 8) + ((WALL_BOT_B - WALL_TOP_B) * y << 8) / sh;
        uint32_t *row = bb + (size_t)y * sw;
        for (int x = x0; x < x1; x++) {
            int t = bayer[y & 3][x & 3] * 16;
            row[x] = RGB((r + t) >> 8, (g + t) >> 8, (b + t) >> 8);
        }
    }

    /* Mascot and name in the middle of the desktop */
    int area_h = sh - TASKBAR_H;
    if (wp_px) {
        int ix = (sw - wp_w) / 2, iy = (area_h - wp_h) / 2 - 24;
        picture(ix, iy, wp_px, wp_w, wp_h, 120);
        const char *name = "Loop-OS";
        text16s((sw - 7 * 16) / 2, iy + wp_h + 6, name, RGB(140, 170, 215), 2);
    } else {
        int scale = sh >= 700 ? 5 : 3;
        int lw = LOGO_W * scale, lh = LOGO_H * scale;
        logo((sw - lw) / 2, (area_h - lh) / 2 - 20, scale, 60);
        text16s((sw - 7 * 16) / 2, (area_h - lh) / 2 + lh, "Loop-OS", RGB(140, 170, 215), 2);
    }
}

static void load_wallpaper(void) {
    struct vnode *n = vfs_lookup("/etc/wallpaper.lwp");
    if (!n || n->size < 12 || n->size > 8 * 1024 * 1024) return;
    uint8_t *buf = kmalloc(n->size);
    if (!buf) return;
    if (vfs_read(n, 0, buf, n->size) != (long)n->size || memcmp(buf, "LWP1", 4) != 0) { kfree(buf); return; }
    uint32_t w = buf[4] | (buf[5] << 8) | (buf[6] << 16) | ((uint32_t)buf[7] << 24);
    uint32_t h = buf[8] | (buf[9] << 8) | (buf[10] << 16) | ((uint32_t)buf[11] << 24);
    if (w == 0 || h == 0 || w > 2048 || h > 2048 || 12 + (size_t)w * h * 4 != n->size) { kfree(buf); return; }
    wp_px = (const uint32_t *)(buf + 12);
    wp_w = (int)w;
    wp_h = (int)h;
}

/* ------------------------------------------------------------------ */
/* Procedural icons, drawn on a 16x16 grid scaled to 'size' pixels */
static void shade(int x, int y, int w, int h, int alpha) {       /* darken the area by alpha/256 */
    int x0 = x < clip_x0 ? clip_x0 : x, y0 = y < clip_y0 ? clip_y0 : y;
    int x1 = x + w > clip_x1 ? clip_x1 : x + w, y1 = y + h > clip_y1 ? clip_y1 : y + h;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = bb + (size_t)j * sw;
        for (int i = x0; i < x1; i++) {
            uint32_t p = row[i];
            uint32_t r = ((p >> 16) & 255) * (256 - alpha) >> 8;
            uint32_t g = ((p >> 8) & 255) * (256 - alpha) >> 8;
            uint32_t b = (p & 255) * (256 - alpha) >> 8;
            row[i] = (r << 16) | (g << 8) | b;
        }
    }
}

static void ru(int ix, int iy, int size, int x, int y, int w, int h, uint32_t c) {
    int px = ix + x * size / 16, py = iy + y * size / 16;
    int pw = (x + w) * size / 16 - x * size / 16, ph = (y + h) * size / 16 - y * size / 16;
    if (pw < 1) pw = 1;
    if (ph < 1) ph = 1;
    fill(px, py, pw, ph, c);
}

static void disc(int cx, int cy, int r, int r_in, uint32_t c) {      /* filled circle, or ring when r_in > 0 */
    for (int j = -r; j <= r; j++) {
        for (int i = -r; i <= r; i++) {
            int d = i * i + j * j;
            if (d <= r * r && d >= r_in * r_in) fill(cx + i, cy + j, 1, 1, c);
        }
    }
}

static void draw_icon(const char *name, int ix, int iy, int size) {
    if (!strcmp(name, "term")) {
        ru(ix, iy, size, 1, 2, 14, 12, RGB(20, 24, 36));
        ru(ix, iy, size, 1, 2, 14, 2, RGB(90, 150, 230));
        ru(ix, iy, size, 3, 6, 1, 1, RGB(120, 230, 150)); ru(ix, iy, size, 4, 7, 1, 1, RGB(120, 230, 150));
        ru(ix, iy, size, 3, 8, 1, 1, RGB(120, 230, 150));
        ru(ix, iy, size, 7, 9, 4, 1, RGB(200, 210, 230));
    } else if (!strcmp(name, "note")) {
        ru(ix, iy, size, 3, 1, 10, 14, RGB(240, 242, 248));
        ru(ix, iy, size, 3, 1, 10, 2, RGB(230, 180, 70));
        for (int k = 0; k < 4; k++) ru(ix, iy, size, 5, 5 + k * 2, 6, 1, RGB(120, 130, 160));
    } else if (!strcmp(name, "files")) {
        ru(ix, iy, size, 1, 3, 6, 3, RGB(220, 170, 60));
        ru(ix, iy, size, 1, 5, 14, 9, RGB(245, 200, 80));
        ru(ix, iy, size, 1, 5, 14, 1, RGB(255, 225, 130));
    } else if (!strcmp(name, "calc")) {
        ru(ix, iy, size, 3, 1, 10, 14, RGB(70, 78, 104));
        ru(ix, iy, size, 4, 2, 8, 3, RGB(140, 230, 170));
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) ru(ix, iy, size, 4 + c * 3, 6 + r * 3, 2, 2, RGB(200, 208, 228));
    } else if (!strcmp(name, "tasks")) {
        ru(ix, iy, size, 1, 2, 14, 12, RGB(30, 36, 56));
        ru(ix, iy, size, 3, 9, 2, 4, RGB(90, 210, 120)); ru(ix, iy, size, 6, 6, 2, 7, RGB(90, 210, 120));
        ru(ix, iy, size, 9, 8, 2, 5, RGB(230, 190, 70)); ru(ix, iy, size, 12, 4, 2, 9, RGB(220, 90, 90));
    } else if (!strcmp(name, "net")) {
        for (int k = 0; k < 4; k++) ru(ix, iy, size, 2 + k * 3, 12 - (2 + k * 3), 2, 2 + k * 3, RGB(90, 210, 120));
    } else if (!strcmp(name, "info")) {
        int r = size / 2 - 1;
        disc(ix + size / 2, iy + size / 2, r, 0, RGB(70, 130, 220));
        ru(ix, iy, size, 7, 3, 2, 2, RGB(255, 255, 255));
        ru(ix, iy, size, 7, 6, 2, 7, RGB(255, 255, 255));
    } else if (!strcmp(name, "reboot")) {
        int r = size / 2 - 1;
        disc(ix + size / 2, iy + size / 2, r, r - size / 6 - 1, RGB(240, 170, 60));
        ru(ix, iy, size, 9, 1, 5, 4, C_MENU_BG);
        ru(ix, iy, size, 10, 1, 4, 1, RGB(240, 170, 60)); ru(ix, iy, size, 13, 1, 1, 4, RGB(240, 170, 60));
    } else if (!strcmp(name, "power")) {
        int r = size / 2 - 1;
        disc(ix + size / 2, iy + size / 2, r, r - size / 6 - 1, RGB(230, 80, 80));
        ru(ix, iy, size, 6, 0, 4, 8, C_MENU_BG);
        ru(ix, iy, size, 7, 0, 2, 8, RGB(230, 80, 80));
    } else if (!strcmp(name, "web")) {
        int r = size / 2 - 1;
        int cx = ix + size / 2, cy = iy + size / 2;
        disc(cx, cy, r, 0, RGB(60, 130, 220));
        disc(cx, cy, r, r - 1, RGB(200, 225, 255));
        ru(ix, iy, size, 2, 8, 12, 1, RGB(200, 225, 255));
        ru(ix, iy, size, 8, 1, 1, 14, RGB(200, 225, 255));
        ru(ix, iy, size, 5, 4, 6, 1, RGB(200, 225, 255)); ru(ix, iy, size, 5, 12, 6, 1, RGB(200, 225, 255));
    } else if (!strcmp(name, "image")) {
        ru(ix, iy, size, 1, 2, 14, 12, RGB(240, 242, 248));
        ru(ix, iy, size, 2, 3, 12, 10, RGB(110, 180, 240));
        ru(ix, iy, size, 2, 9, 12, 4, RGB(90, 190, 110));
        ru(ix, iy, size, 10, 4, 3, 3, RGB(255, 225, 100));
    } else {
        ru(ix, iy, size, 3, 2, 10, 12, RGB(120, 130, 160));
    }
}

static int in_rect(int x, int y, int rx, int ry, int rw, int rh);

/* Desktop icons: the first menu entries that are programs, in a column on the left */
#define DESK_ICON 44
#define DESK_STEP 92
static int desk_sel = -1;
static uint64_t desk_click_tick = 0;

static int desk_count(void) {
    int n = 0;
    for (int i = 0; i < menu_count && n < 6; i++) if (menu[i].cmd[0] != '@' && menu[i].icon[0]) n++;
    return n;
}

static int desk_menu_index(int k) {                   /* k-th desktop icon -> menu index */
    int n = 0;
    for (int i = 0; i < menu_count; i++) {
        if (menu[i].cmd[0] == '@' || !menu[i].icon[0]) continue;
        if (n++ == k) return i;
    }
    return -1;
}

static int desk_hit(int mx, int my) {
    for (int k = 0; k < desk_count(); k++) {
        int x = 16, y = 16 + k * DESK_STEP;
        if (in_rect(mx, my, x, y, 84, 80)) return k;
    }
    return -1;
}

static void draw_desktop_icons(void) {
    for (int k = 0; k < desk_count(); k++) {
        int mi = desk_menu_index(k);
        int x = 16, y = 16 + k * DESK_STEP;
        if (k == desk_sel) { shade(x, y, 84, 80, 0); fill(x, y, 84, 80, RGB(30, 60, 110)); }
        draw_icon(menu[mi].icon, x + (84 - DESK_ICON) / 2, y + 6, DESK_ICON);
        int len = (int)strlen(menu[mi].label);
        if (len > 10) len = 10;
        char label[12];
        memcpy(label, menu[mi].label, (size_t)len);
        label[len] = '\0';
        int tx = x + (84 - len * 8) / 2;
        text16(tx + 1, y + 56 + 1, label, RGB(8, 10, 20));
        text16(tx, y + 56, label, C_TEXT);
    }
}

static void draw_window(int slot) {
    struct window *w = &wins[slot];
    int active = slot == focused;
    int ow = w->w + 2, oh = w->h + TITLE_H + 1;
    uint32_t tc = active ? C_TITLE_ON : C_TITLE_OFF;

    /* soft drop shadow: three nested translucent layers */
    shade(w->x + 3 - 6, w->y + 4 - 6, ow + 12, oh + 12, 22);
    shade(w->x + 3 - 4, w->y + 4 - 4, ow + 8, oh + 8, 30);
    shade(w->x + 3 - 2, w->y + 4 - 2, ow + 4, oh + 4, 44);

    fill(w->x, w->y, ow, oh, C_BORDER);
    for (int row = 0; row < TITLE_H - 1; row++) {                /* title bar with a light top gradient */
        int lift = (TITLE_H - 1 - row) * 40 / TITLE_H;
        uint32_t r = ((tc >> 16) & 255) + lift / 2, g = ((tc >> 8) & 255) + lift / 2, b = (tc & 255) + lift;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        fill(w->x + 1, w->y + 1 + row, ow - 2, 1, (r << 16) | (g << 8) | b);
    }
    text16(w->x + 8, w->y + 3, w->title, C_TEXT);

    /* minimize and close buttons */
    int bx = w->x + ow - 20;
    fill(bx, w->y + 3, 16, 16, active ? RGB(190, 60, 60) : RGB(110, 60, 70));
    text(bx + 4, w->y + 7, "x", C_TEXT, 1);
    bx -= 20;
    fill(bx, w->y + 3, 16, 16, active ? RGB(70, 110, 190) : RGB(80, 86, 110));
    text(bx + 4, w->y + 7, "_", C_TEXT, 1);

    /* client area */
    int cx = w->x + 1, cy = w->y + TITLE_H;
    int x0 = cx < clip_x0 ? clip_x0 : cx, y0 = cy < clip_y0 ? clip_y0 : cy;
    int x1 = cx + w->w > clip_x1 ? clip_x1 : cx + w->w;
    int y1 = cy + w->h > clip_y1 ? clip_y1 : cy + w->h;
    for (int j = y0; j < y1; j++) {
        memcpy(&bb[(size_t)j * sw + x0], &w->px[(size_t)(j - cy) * w->w + (x0 - cx)], (size_t)(x1 - x0) * 4);
    }
}

static void clock_text(char *date, char *tm) {
    tm[0] = (char)('0' + now_time.hour / 10); tm[1] = (char)('0' + now_time.hour % 10);
    tm[2] = ':';
    tm[3] = (char)('0' + now_time.minute / 10); tm[4] = (char)('0' + now_time.minute % 10);
    tm[5] = '\0';
    date[0] = (char)('0' + now_time.day / 10); date[1] = (char)('0' + now_time.day % 10);
    date[2] = '/';
    date[3] = (char)('0' + now_time.month / 10); date[4] = (char)('0' + now_time.month % 10);
    date[5] = '/';
    int y = now_time.year;
    date[6] = (char)('0' + y / 1000 % 10); date[7] = (char)('0' + y / 100 % 10);
    date[8] = (char)('0' + y / 10 % 10); date[9] = (char)('0' + y % 10);
    date[10] = '\0';
}

/* Geometry of the taskbar buttons (one per window) */
static int task_button_w(void) {
    int avail = sw - START_X - START_W - 12 - 150;
    int n = zcount ? zcount : 1;
    int bw = avail / n - 4;
    return bw > 160 ? 160 : bw;
}

/* Network status: 0 no card, 1 no cable, 2 waiting for an address, 3 online */
#define NET_ICON_W 34
static int net_state = -1;

static int net_state_now(void) {
    const struct netinfo *n = net_info();
    if (!n->present) return 0;
    if (!n->link) return 1;
    return n->configured ? 3 : 2;
}

static void draw_net_icon(int x, int y) {
    static const uint32_t cols[4] = { RGB(120, 124, 140), RGB(220, 70, 70), RGB(230, 190, 70), RGB(90, 210, 120) };
    uint32_t c = cols[net_state < 0 ? 0 : net_state];
    int lit = net_state == 3 ? 4 : net_state == 2 ? 2 : net_state == 1 ? 1 : 0;
    for (int i = 0; i < 4; i++) {                       /* four ascending bars, like a signal meter */
        int h = 5 + i * 4;
        fill(x + i * 7, y + 20 - h, 5, h, i < lit ? c : RGB(52, 58, 80));
    }
    if (net_state == 0 || net_state == 1) {             /* a cross over the bars */
        for (int k = 0; k < 18; k++) {
            fill(x + 5 + k, y + 3 + k, 3, 2, RGB(220, 70, 70));
            fill(x + 5 + k, y + 20 - k, 3, 2, RGB(220, 70, 70));
        }
    }
}

static void draw_taskbar(void) {
    int y = sh - TASKBAR_H;
    fill(0, y, sw, TASKBAR_H, C_TASKBAR);
    fill(0, y, sw, 2, C_ACCENT);

    /* start button */
    int hot = menu_open;
    fill(START_X, y + 4, START_W, TASKBAR_H - 8, hot ? C_MENU_HOT : C_BTN);
    logo_small(START_X + 6, y + 6, TASKBAR_H - 12);
    text16(START_X + 34, y + 10, "Iniciar", C_TEXT);

    /* window buttons, in creation order */
    int bw = task_button_w();
    int bx = START_X + START_W + 12;
    for (int s = 0; s < MAX_WINDOWS; s++) {
        if (!wins[s].used) continue;
        int on = s == focused && !wins[s].minimized;
        fill(bx, y + 5, bw, TASKBAR_H - 10, on ? C_TITLE_ON : C_BTN);
        char label[24];
        int maxc = (bw - 12) / 8;
        if (maxc > 22) maxc = 22;
        int i = 0;
        while (wins[s].title[i] && i < maxc) { label[i] = wins[s].title[i]; i++; }
        label[i] = '\0';
        text16(bx + 6, y + 10, label, wins[s].minimized ? C_DIM : C_TEXT);
        bx += bw + 4;
    }

    /* network icon */
    draw_net_icon(sw - 90 - NET_ICON_W - 4, y + 6);

    /* clock */
    char date[12], tm[8];
    clock_text(date, tm);
    text(sw - 85, y + 3, tm, C_TEXT, 2);
    text(sw - 90 + 5, y + 23, date, C_DIM, 1);
}

static void draw_menu(void) {
    if (!menu_open) return;
    int y0 = menu_y();
    fill(START_X, y0, MENU_W, menu_height(), C_ACCENT);
    fill(START_X + 1, y0 + 1, MENU_W - 2, menu_height() - 2, C_MENU_BG);
    for (int i = 0; i < menu_count; i++) {
        int iy = y0 + 4 + i * MENU_ITEM_H;
        if (i == menu_hot) fill(START_X + 4, iy, MENU_W - 8, MENU_ITEM_H, C_MENU_HOT);
        if (menu[i].icon[0]) draw_icon(menu[i].icon, START_X + 10, iy + (MENU_ITEM_H - 18) / 2, 18);
        text16(START_X + 38, iy + (MENU_ITEM_H - 16) / 2, menu[i].label, C_TEXT);
    }
}

static void compose(void) {
    if (dx1 <= dx0) return;
    clip_x0 = dx0; clip_y0 = dy0; clip_x1 = dx1; clip_y1 = dy1;
    draw_wallpaper();
    draw_desktop_icons();
    for (int i = 0; i < zcount; i++) {
        if (!wins[zorder[i]].minimized) draw_window(zorder[i]);
    }
    draw_taskbar();
    draw_menu();
    fb_blit(dx0, dy0, dx1 - dx0, dy1 - dy0, bb + (size_t)dy0 * sw + dx0, sw);
    dx0 = dy0 = dx1 = dy1 = 0;
}

/* ------------------------------------------------------------------ */
/* Input */
static int in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void set_menu(int open) {
    if (open == menu_open) return;
    if (open) load_menu();
    menu_open = open;
    menu_hot = -1;
    damage(START_X, menu_y(), MENU_W, menu_height());
    damage(START_X, sh - TASKBAR_H, START_W, TASKBAR_H);
}

static void taskbar_click(int mx) {
    int bw = task_button_w();
    int bx = START_X + START_W + 12;
    for (int s = 0; s < MAX_WINDOWS; s++) {
        if (!wins[s].used) continue;
        if (mx >= bx && mx < bx + bw) {
            struct window *w = &wins[s];
            if (w->minimized) {
                w->minimized = 0;
                focus_window(s);
                damage_window(w);
            } else if (s == focused) {
                w->minimized = 1;
                damage_window(w);
                focused = -1;
                for (int i = zcount - 1; i >= 0; i--) {
                    if (!wins[zorder[i]].minimized) { focused = zorder[i]; break; }
                }
            } else {
                focus_window(s);
            }
            damage(0, sh - TASKBAR_H, sw, TASKBAR_H);
            return;
        }
        bx += bw + 4;
    }
}

static void mouse_press(int mx, int my) {
    if (menu_open) {
        int y0 = menu_y();
        if (in_rect(mx, my, START_X, y0, MENU_W, menu_height())) {
            int i = (my - y0 - 4) / MENU_ITEM_H;
            set_menu(0);
            if (i >= 0 && i < menu_count) launch(menu[i].cmd);
            return;
        }
        set_menu(0);
        if (in_rect(mx, my, START_X, sh - TASKBAR_H, START_W, TASKBAR_H)) return;
    }

    if (my >= sh - TASKBAR_H) {
        if (in_rect(mx, my, sw - 90 - NET_ICON_W - 8, sh - TASKBAR_H, NET_ICON_W + 8, TASKBAR_H)) launch("/bin/rede");
        else if (in_rect(mx, my, START_X, sh - TASKBAR_H + 4, START_W, TASKBAR_H - 8)) set_menu(1);
        else taskbar_click(mx);
        return;
    }

    int s = window_at(mx, my);
    if (s < 0) {
        int k = desk_hit(mx, my);
        uint64_t t = timer_ticks();
        if (k >= 0 && k == desk_sel && t - desk_click_tick < 50) {          /* double click */
            desk_sel = -1;
            launch(menu[desk_menu_index(k)].cmd);
        } else {
            desk_sel = k;
            desk_click_tick = t;
        }
        damage(0, 0, 110, 16 + 6 * DESK_STEP);
        return;
    }
    desk_sel = -1;
    struct window *w = &wins[s];
    focus_window(s);

    int rel_y = my - w->y;
    if (rel_y < TITLE_H) {
        int ow = w->w + 2;
        if (in_rect(mx, my, w->x + ow - 20, w->y + 3, 16, 16)) {
            push_event(w, WIN_EV_CLOSE, 0, 0, 0);
        } else if (in_rect(mx, my, w->x + ow - 40, w->y + 3, 16, 16)) {
            w->minimized = 1;
            damage_window(w);
            focused = -1;
            for (int i = zcount - 1; i >= 0; i--) {
                if (!wins[zorder[i]].minimized) { focused = zorder[i]; break; }
            }
            damage(0, sh - TASKBAR_H, sw, TASKBAR_H);
        } else {
            drag_slot = s;
            drag_dx = mx - w->x;
            drag_dy = my - w->y;
        }
        return;
    }
    capture_slot = s;
    push_event(w, WIN_EV_MOUSE, mx - (w->x + 1), my - (w->y + TITLE_H), 1);
}

static void handle_mouse(void) {
    if (!mouse_present()) return;
    int mx = mouse_x(), my = mouse_y();
    int btn = mouse_buttons() & MOUSE_BTN_LEFT;

    if (menu_open) {
        int y0 = menu_y();
        int hot = in_rect(mx, my, START_X, y0 + 4, MENU_W, menu_count * MENU_ITEM_H)
                      ? (my - y0 - 4) / MENU_ITEM_H : -1;
        if (hot != menu_hot) {
            menu_hot = hot;
            damage(START_X, y0, MENU_W, menu_height());
        }
    }

    if (btn && !prev_btn) {
        mouse_press(mx, my);
    } else if (btn && prev_btn) {
        if (drag_slot >= 0) {
            struct window *w = by_slot(drag_slot);
            if (w && (mx != prev_x || my != prev_y)) {
                damage_window(w);
                w->x = mx - drag_dx;
                w->y = my - drag_dy;
                if (w->y < 0) w->y = 0;
                if (w->y > sh - TASKBAR_H - TITLE_H) w->y = sh - TASKBAR_H - TITLE_H;
                if (w->x < -w->w + 40) w->x = -w->w + 40;
                if (w->x > sw - 40) w->x = sw - 40;
                damage_window(w);
            }
        } else if (capture_slot >= 0 && (mx != prev_x || my != prev_y)) {
            struct window *w = by_slot(capture_slot);
            if (w) push_event(w, WIN_EV_MOUSE, mx - (w->x + 1), my - (w->y + TITLE_H), 1);
        }
    } else if (!btn && prev_btn) {
        struct window *w = by_slot(capture_slot);
        if (w) push_event(w, WIN_EV_MOUSE, mx - (w->x + 1), my - (w->y + TITLE_H), 0);
        capture_slot = -1;
        drag_slot = -1;
    }
    prev_x = mx; prev_y = my; prev_btn = btn;
}

static void handle_keyboard(void) {
    while (keyboard_has_key()) {
        int c = keyboard_getchar();
        if (c < 0) break;
        struct window *w = by_slot(focused);
        if (w && !w->minimized) push_event(w, WIN_EV_KEY, c, 0, 0);
    }
}

static void update_clock(void) {
    static uint64_t last_tick = 0;
    uint64_t t = timer_ticks();
    if (clock_minute >= 0 && t - last_tick < 100) return;     /* once a second */
    last_tick = t;
    int ns = net_state_now();
    if (ns != net_state) {
        net_state = ns;
        damage(sw - 90 - NET_ICON_W - 8, sh - TASKBAR_H, NET_ICON_W + 8, TASKBAR_H);
    }
    rtc_read(&now_time);
    if (now_time.minute != clock_minute) {
        clock_minute = now_time.minute;
        damage(sw - 90, sh - TASKBAR_H, 90, TASKBAR_H);
    }
}

/* ------------------------------------------------------------------ */
int wm_init(void) {
    sw = (int)fb_width();
    sh = (int)fb_height();
    if (sw <= 0 || sh <= 0) return -1;
    bb = kmalloc((size_t)sw * sh * 4);
    if (!bb) return -1;
    if (fb_rgb(0x12, 0x34, 0x56) != 0x123456) {
        kprintf("wm: unusual framebuffer channel layout, colours may look wrong\n");
    }
    memset(wins, 0, sizeof(wins));
    load_wallpaper();
    damage(0, 0, sw, sh);
    return 0;
}

void wm_run(void) {
    update_clock();
    compose();

    for (;;) {
        net_poll();
        handle_keyboard();
        handle_mouse();
        update_clock();
        compose();

        /* Collect programs started from the menu once they exit */
        int st;
        while (task_wait(-1, &st, 1) > 0) {}

        task_block(W_SLEEP, 1);
    }
}
