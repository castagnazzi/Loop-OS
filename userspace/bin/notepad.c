/* notepad: a tiny text editor. Type to append, Backspace to delete, click
 * "Salvar" to write the file (default /tmp/nota.txt, or the first argument). */

#include <gui.h>

#define TEXT_MAX 8192
#define W 520
#define H 360
#define TOP 30
#define BG RGB(250, 250, 244)

static char text[TEXT_MAX];
static int len = 0;
static const char *path = "/tmp/nota.txt";
static const char *status = "";

static void load(void) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    long n = read(fd, text, TEXT_MAX - 1);
    if (n > 0) len = (int)n;
    close(fd);
}

static void save(void) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { status = "erro ao salvar"; return; }
    write(fd, text, (size_t)len);
    close(fd);
    status = "salvo";
}

static void render(struct gwin *g) {
    gui_fill(g, 0, 0, W, H, BG);
    gui_fill(g, 0, 0, W, TOP, RGB(226, 228, 236));
    gui_button(g, 4, 4, 70, 22, "Salvar", 0);
    gui_text(g, 84, 8, path, RGB(40, 44, 60), 1);
    gui_text(g, W - 8 * (int)strlen(status) - 8, 8, status, RGB(30, 120, 60), 1);

    int x = 6, y = TOP + 6;
    for (int i = 0; i < len; i++) {
        if (text[i] == '\n' || x > W - 14) { x = 6; y += 11; if (text[i] == '\n') continue; }
        if (y > H - 12) break;
        gui_char(g, x, y, text[i], RGB(20, 24, 36), 1);
        x += 8;
    }
    gui_fill(g, x, y + 9, 8, 2, RGB(40, 90, 200));
    gui_present(g);
}

int main(int argc, char **argv) {
    if (argc > 1) path = argv[1];
    load();

    struct gwin g;
    if (gui_open(&g, W, H, "Bloco de Notas") < 0) return 1;
    render(&g);

    struct win_event ev;
    while (gui_wait(&g, &ev) > 0) {
        if (ev.type == WIN_EV_CLOSE) return 0;
        if (ev.type == WIN_EV_KEY) {
            char c = (char)ev.a;
            status = "";
            if ((c == '\b' || c == 127) && len > 0) len--;
            else if ((c == '\n' || c == '\r') && len < TEXT_MAX - 1) text[len++] = '\n';
            else if (c >= 32 && c < 127 && len < TEXT_MAX - 1) text[len++] = c;
        } else if (ev.type == WIN_EV_MOUSE && ev.c == 1 && gui_inside(ev.a, ev.b, 4, 4, 70, 22)) {
            save();
        }
        render(&g);
    }
    return 0;
}
