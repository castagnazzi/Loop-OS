/* about: "Sobre o Loop-OS" - signature and basic system information */

#include <gui.h>
#include "../../kernel/drivers/logo.h"

#define W 480
#define H 400

#define C_BG     RGB(26, 30, 44)
#define C_TEXT   RGB(225, 232, 245)
#define C_DIM    RGB(140, 156, 186)
#define C_ACCENT RGB(120, 190, 255)

static struct gwin g;

/* Mascot at 2x, blended over the background */
static void draw_logo(int x, int y) {
    for (int j = 0; j < LOGO_H * 2; j++) {
        for (int i = 0; i < LOGO_W * 2; i++) {
            uint32_t p = logo_argb[(j / 2) * LOGO_W + (i / 2)];
            unsigned a = p >> 24;
            if (!a) continue;
            int px = x + i, py = y + j;
            uint32_t *d = &g.px[(size_t)py * g.w + px];
            unsigned r = (((*d >> 16) & 0xFF) * (255 - a) + ((p >> 16) & 0xFF) * a) / 255;
            unsigned gg = (((*d >> 8) & 0xFF) * (255 - a) + ((p >> 8) & 0xFF) * a) / 255;
            unsigned b = ((*d & 0xFF) * (255 - a) + (p & 0xFF) * a) / 255;
            *d = RGB(r, gg, b);
        }
    }
}

static void row(int y, const char *label, const char *value) {
    gui_text(&g, 28, y, label, C_DIM, 1);
    gui_text(&g, 156, y, value, C_TEXT, 1);
}

static void render(void) {
    struct sys_sysinfo si;
    sysinfo(&si);

    gui_fill(&g, 0, 0, W, H, C_BG);
    gui_fill(&g, 0, 0, W, 136, RGB(34, 40, 62));
    gui_fill(&g, 0, 136, W, 2, RGB(80, 160, 255));

    draw_logo(28, 26);
    gui_text(&g, 128, 26, si.os, C_TEXT, 2);
    char line[96];
    snprintf(line, sizeof(line), "Vers\xe3o %s", si.version);
    gui_text(&g, 130, 66, line, C_DIM, 1);
    gui_text(&g, 130, 94, "By | Castagnazzi @ IA", C_ACCENT, 1);

    int y = 156;
    row(y, "Sistema", si.os);                                  y += 24;
    snprintf(line, sizeof(line), "%s (%s)", si.kernel, si.version);
    row(y, "Kernel", line);                                    y += 24;
    row(y, "Processador", si.cpu[0] ? si.cpu : si.vendor);     y += 24;

    unsigned long total = (unsigned long)(si.mem_total_kb / 1024);
    unsigned long used = (unsigned long)((si.mem_total_kb - si.mem_free_kb) / 1024);
    snprintf(line, sizeof(line), "%lu MB usados de %lu MB", used, total);
    row(y, "Mem\xf3ria", line);                                   y += 24;

    snprintf(line, sizeof(line), "%u x %u", si.fb_w, si.fb_h);
    row(y, "Resolu\xe7\xe3o", line);                                 y += 24;

    unsigned long secs = (unsigned long)(si.uptime_ms / 1000);
    snprintf(line, sizeof(line), "%02lu:%02lu:%02lu", secs / 3600, (secs / 60) % 60, secs % 60);
    row(y, "Tempo ligado", line);                              y += 24;

    snprintf(line, sizeof(line), "%u em execu\xe7\xe3o", si.ntasks);
    row(y, "Tarefas", line);                                   y += 24;
    snprintf(line, sizeof(line), "%u", si.ndisks);
    row(y, "Discos", line);

    gui_button(&g, W - 96, H - 40, 80, 28, "OK", 0);
    gui_present(&g);
}

int main(void) {
    if (gui_open(&g, W, H, "Sobre o Loop-OS") < 0) return 1;
    render();

    unsigned long last = uptime_ms();
    for (;;) {
        struct win_event ev;
        int r;
        while ((r = gui_poll(&g, &ev)) > 0) {
            if (ev.type == WIN_EV_CLOSE) return 0;
            if (ev.type == WIN_EV_MOUSE && ev.c == 1 && gui_inside(ev.a, ev.b, W - 96, H - 40, 80, 28)) return 0;
        }
        if (r < 0) return 0;
        if (uptime_ms() - last >= 1000) {
            last = uptime_ms();
            render();
        }
        sleep_ms(50);
    }
}
