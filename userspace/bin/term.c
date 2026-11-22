/* term: a terminal emulator in a window. Runs /bin/sh on two pipes, shows
 * what it prints in a character grid and sends typed keys to it. */

#include <gui.h>

#define COLS 78
#define ROWS 36
#define CELL_W 8
#define CELL_H 10

#define BG RGB(12, 16, 28)
#define FG RGB(205, 214, 232)

static char grid[ROWS][COLS];
static int cx = 0, cy = 0;
static int esc = 0, esc_arg = 0;

static void clear_grid(void) {
    memset(grid, ' ', sizeof(grid));
    cx = cy = 0;
}

static void newline(void) {
    cx = 0;
    if (++cy >= ROWS) {
        cy = ROWS - 1;
        memmove(grid[0], grid[1], (ROWS - 1) * COLS);
        memset(grid[ROWS - 1], ' ', COLS);
    }
}

static void put(char c) {
    if (esc == 1) { esc = (c == '[') ? 2 : 0; esc_arg = 0; return; }
    if (esc == 2) {
        if (c >= '0' && c <= '9') { esc_arg = esc_arg * 10 + (c - '0'); return; }
        if (c == 'J' && esc_arg == 2) clear_grid();
        else if (c == 'H') cx = cy = 0;
        if (c != ';') esc = 0;
        return;
    }
    switch (c) {
    case 0x1B: esc = 1; return;
    case '\n': newline(); return;
    case '\r': cx = 0; return;
    case '\b': if (cx > 0) cx--; return;
    case '\t': do { put(' '); } while (cx & 7); return;
    default: break;
    }
    if (c < 32 || c > 126) return;
    if (cx >= COLS) newline();
    grid[cy][cx++] = c;
}

static void render(struct gwin *g) {
    gui_fill(g, 0, 0, g->w, g->h, BG);
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (grid[r][c] != ' ') gui_char(g, 4 + c * CELL_W, 4 + r * CELL_H + 1, grid[r][c], FG, 1);
        }
    }
    gui_fill(g, 4 + cx * CELL_W, 4 + cy * CELL_H + CELL_H - 2, CELL_W, 2, RGB(120, 220, 160));
    gui_present(g);
}

int main(void) {
    struct gwin g;
    if (gui_open(&g, COLS * CELL_W + 8, ROWS * CELL_H + 8, "Terminal") < 0) return 1;

    int to_sh[2], from_sh[2];
    if (pipe(to_sh) < 0 || pipe(from_sh) < 0) return 1;

    int pid = fork();
    if (pid == 0) {
        dup2(to_sh[0], STDIN_FILENO);
        dup2(from_sh[1], STDOUT_FILENO);
        dup2(from_sh[1], STDERR_FILENO);
        for (int fd = 3; fd < 16; fd++) close(fd);
        char *argv[] = { "sh", NULL };
        execve("/bin/sh", argv);
        exit(127);
    }
    close(to_sh[0]);
    close(from_sh[1]);

    clear_grid();
    render(&g);

    for (;;) {
        int changed = 0;
        int n;
        while ((n = fd_avail(from_sh[0])) > 0) {
            char buf[128];
            long r = read(from_sh[0], buf, n > 128 ? 128 : (size_t)n);
            if (r <= 0) break;
            for (long i = 0; i < r; i++) put(buf[i]);
            changed = 1;
        }
        if (n < 0) break;                         /* the shell exited */

        struct win_event ev;
        int got = 0, r;
        while ((r = gui_poll(&g, &ev)) > 0) {
            got = 1;
            if (ev.type == WIN_EV_KEY) {
                char c = (char)ev.a;
                write(to_sh[1], &c, 1);
            } else if (ev.type == WIN_EV_CLOSE) {
                kill(pid, 9);
                waitpid(pid, NULL, 0);
                return 0;
            }
        }
        if (r < 0) break;

        if (changed) render(&g);
        else if (!got) sleep_ms(10);
    }
    waitpid(pid, NULL, 0);
    return 0;
}
