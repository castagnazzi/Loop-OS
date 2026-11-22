/* taskmgr: lists the running tasks; select one and press "Encerrar" */

#include <gui.h>

#define W 380
#define H 380
#define ROW 18
#define LIST_Y 30
#define MAX_ROWS 16

static const char *state_name(int s) {
    switch (s) {
    case 1: return "pronto";
    case 2: return "rodando";
    case 3: return "espera";
    case 4: return "zumbi";
    default: return "?";
    }
}

static struct sys_taskinfo rows[MAX_ROWS];
static int nrows = 0, selected = -1;

static void refresh(void) {
    nrows = 0;
    struct sys_taskinfo t;
    for (int i = 0; i < 32 && nrows < MAX_ROWS; i++) {
        if (taskinfo(i, &t) == 0) rows[nrows++] = t;
    }
}

static void render(struct gwin *g) {
    gui_fill(g, 0, 0, W, H, RGB(30, 34, 50));
    gui_text(g, 8, 7, "PID  Nome                Estado", RGB(150, 170, 200), 1);
    for (int i = 0; i < nrows; i++) {
        int y = LIST_Y + i * ROW;
        if (i == selected) gui_fill(g, 4, y - 1, W - 8, ROW, RGB(52, 104, 190));
        char line[64];
        snprintf(line, sizeof(line), "%-4d %-19s %s", rows[i].pid, rows[i].name, state_name(rows[i].state));
        gui_text(g, 8, y, line, RGB(235, 240, 250), 1);
    }
    gui_button(g, 8, H - 34, 110, 26, "Encerrar", 0);
    char info[32];
    snprintf(info, sizeof(info), "%d tarefas", nrows);
    gui_text(g, W - 8 - 8 * (int)strlen(info), H - 29, info, RGB(150, 170, 200), 1);
    gui_present(g);
}

int main(void) {
    struct gwin g;
    if (gui_open(&g, W, H, "Gerenciador de Tarefas") < 0) return 1;
    refresh();
    render(&g);

    unsigned long last = uptime_ms();
    for (;;) {
        struct win_event ev;
        int r, changed = 0;
        while ((r = gui_poll(&g, &ev)) > 0) {
            if (ev.type == WIN_EV_CLOSE) return 0;
            if (ev.type != WIN_EV_MOUSE || ev.c != 1) continue;
            changed = 1;
            if (gui_inside(ev.a, ev.b, 8, H - 34, 110, 26)) {
                if (selected >= 0 && rows[selected].pid > 1 && rows[selected].pid != getpid()) {
                    kill(rows[selected].pid, 9);
                    selected = -1;
                    refresh();
                }
            } else if (ev.b >= LIST_Y - 1 && ev.b < LIST_Y - 1 + nrows * ROW) {
                selected = (ev.b - (LIST_Y - 1)) / ROW;
            }
        }
        if (r < 0) return 0;
        if (uptime_ms() - last > 700) {
            last = uptime_ms();
            refresh();
            if (selected >= nrows) selected = -1;
            changed = 1;
        }
        if (changed) render(&g);
        sleep_ms(30);
    }
}
