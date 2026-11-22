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
    damage(w->x, w->y, w->w + 2, w->h + TITLE_H + 1);
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

static void draw_window(int slot) {
    struct window *w = &wins[slot];
    int active = slot == focused;
    int ow = w->w + 2, oh = w->h + TITLE_H + 1;
    uint32_t tc = active ? C_TITLE_ON : C_TITLE_OFF;

    fill(w->x, w->y, ow, oh, C_BORDER);
    fill(w->x + 1, w->y + 1, ow - 2, TITLE_H - 1, tc);
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
    int avail = sw - START_X - START_W - 12 - 110;
    int n = zcount ? zcount : 1;
    int bw = avail / n - 4;
    return bw > 160 ? 160 : bw;
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
        text16(START_X + 16, iy + (MENU_ITEM_H - 16) / 2, menu[i].label, C_TEXT);
    }
}

static void compose(void) {
    if (dx1 <= dx0) return;
    clip_x0 = dx0; clip_y0 = dy0; clip_x1 = dx1; clip_y1 = dy1;
    draw_wallpaper();
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
        if (in_rect(mx, my, START_X, sh - TASKBAR_H + 4, START_W, TASKBAR_H - 8)) set_menu(1);
        else taskbar_click(mx);
        return;
    }

    int s = window_at(mx, my);
    if (s < 0) return;
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
