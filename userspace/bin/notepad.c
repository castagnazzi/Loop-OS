/* notepad: a small text editor with tabs.
 *
 * usage: notepad [file ...]    (default /tmp/nota.txt)
 * Type to append, Backspace to delete. "Salvar" writes the current tab, "Novo" opens
 * an empty one (/tmp/novoN.txt). Click a tab to switch, its [x] to close it. */

#include <gui.h>

#define TEXT_MAX 8192
#define MAX_TABS 6
#define W 560
#define H 380
#define BAR 30
#define TABS_H 26
#define TOP (BAR + TABS_H)
#define BG RGB(250, 250, 244)
#define TAB_W 90

struct tab {
    char path[96];
    char text[TEXT_MAX];
    int len;
    int dirty;
};

static struct tab tabs[MAX_TABS];
static int ntabs = 0, cur = 0;
static const char *status = "";
static int new_count = 1;

static const char *base(const char *p) {
    const char *b = p;
    for (; *p; p++) if (*p == '/') b = p + 1;
    return b;
}

static int open_tab(const char *path) {
    if (ntabs >= MAX_TABS) { status = "limite de abas"; return -1; }
    struct tab *t = &tabs[ntabs];
    memset(t, 0, sizeof(*t));
    strncpy(t->path, path, sizeof(t->path) - 1);
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        long n = read(fd, t->text, TEXT_MAX - 1);
        if (n > 0) t->len = (int)n;
        close(fd);
    }
    cur = ntabs++;
    return cur;
}

static void new_tab(void) {
    char p[64];
    snprintf(p, sizeof(p), "/tmp/novo%d.txt", new_count++);
    open_tab(p);
}

static void close_tab(int i) {
    for (int k = i; k < ntabs - 1; k++) tabs[k] = tabs[k + 1];
    ntabs--;
    if (ntabs == 0) new_tab();
    if (cur >= ntabs) cur = ntabs - 1;
}

static void save(void) {
    struct tab *t = &tabs[cur];
    int fd = open(t->path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { status = "nao e possivel salvar aqui"; return; }
    write(fd, t->text, (size_t)t->len);
    close(fd);
    t->dirty = 0;
    status = "salvo";
}

static void render(struct gwin *g) {
    struct tab *t = &tabs[cur];
    gui_fill(g, 0, 0, W, H, BG);

    /* toolbar */
    gui_fill(g, 0, 0, W, BAR, RGB(226, 228, 236));
    gui_button(g, 4, 4, 64, 22, "Salvar", 0);
    gui_button(g, 74, 4, 54, 22, "Novo", 0);
    gui_text(g, 140, 7, t->path, RGB(40, 44, 60), 1);
    gui_text(g, W - 8 * (int)strlen(status) - 8, 7, status, RGB(30, 120, 60), 1);

    /* tabs */
    gui_fill(g, 0, BAR, W, TABS_H, RGB(200, 204, 218));
    for (int i = 0; i < ntabs; i++) {
        int x = 4 + i * (TAB_W + 3);
        uint32_t bg = i == cur ? BG : RGB(170, 176, 200);
        gui_fill(g, x, BAR + 3, TAB_W, TABS_H - 3, bg);
        char label[16];
        const char *b = base(tabs[i].path);
        int n = 0;
        while (b[n] && n < 8) { label[n] = b[n]; n++; }
        if (tabs[i].dirty) label[n++] = '*';
        label[n] = '\0';
        gui_text(g, x + 5, BAR + 6, label, RGB(30, 34, 50), 1);
        gui_text(g, x + TAB_W - 14, BAR + 6, "x", RGB(150, 50, 50), 1);
    }

    /* text */
    int x = 6, y = TOP + 6;
    for (int i = 0; i < t->len; i++) {
        if (t->text[i] == '\n' || x > W - 14) { x = 6; y += 16; if (t->text[i] == '\n') continue; }
        if (y > H - 18) break;
        gui_char(g, x, y, t->text[i], RGB(20, 24, 36), 1);
        x += 8;
    }
    gui_fill(g, x, y + 14, 8, 2, RGB(40, 90, 200));
    gui_present(g);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        for (int i = 1; i < argc; i++) open_tab(argv[i]);
        cur = 0;
    } else {
        open_tab("/tmp/nota.txt");
    }

    struct gwin g;
    if (gui_open(&g, W, H, "Bloco de Notas") < 0) return 1;
    render(&g);

    struct win_event ev;
    while (gui_wait(&g, &ev) > 0) {
        if (ev.type == WIN_EV_CLOSE) return 0;
        struct tab *t = &tabs[cur];
        if (ev.type == WIN_EV_KEY) {
            unsigned char c = (unsigned char)ev.a;
            status = "";
            if ((c == '\b' || c == 127) && t->len > 0) { t->len--; t->dirty = 1; }
            else if ((c == '\n' || c == '\r') && t->len < TEXT_MAX - 1) { t->text[t->len++] = '\n'; t->dirty = 1; }
            else if (c >= 32 && c != 127 && t->len < TEXT_MAX - 1) { t->text[t->len++] = (char)c; t->dirty = 1; }
        } else if (ev.type == WIN_EV_MOUSE && ev.c == 1) {
            if (gui_inside(ev.a, ev.b, 4, 4, 64, 22)) save();
            else if (gui_inside(ev.a, ev.b, 74, 4, 54, 22)) new_tab();
            else if (ev.b >= BAR && ev.b < TOP) {
                for (int i = 0; i < ntabs; i++) {
                    int x = 4 + i * (TAB_W + 3);
                    if (!gui_inside(ev.a, ev.b, x, BAR + 3, TAB_W, TABS_H - 3)) continue;
                    if (ev.a >= x + TAB_W - 18) close_tab(i);
                    else cur = i;
                    break;
                }
            }
        }
        render(&g);
    }
    return 0;
}
