/* files: a small file browser. Click a folder to enter it, ".." to go up,
 * a file to open it in the notepad. */

#include <gui.h>

#define W 420
#define H 340
#define ROW 14
#define LIST_Y 34
#define MAX_ENTRIES 64
#define VISIBLE ((H - LIST_Y - 8) / ROW)

struct entry {
    char name[48];
    int is_dir;
};

static char dir[96] = "/";
static struct entry entries[MAX_ENTRIES];
static int count = 0;
static int scroll = 0;

static void join(char *out, size_t max, const char *base, const char *name) {
    snprintf(out, max, "%s%s%s", base, base[strlen(base) - 1] == '/' ? "" : "/", name);
}

static void load(void) {
    count = 0;
    scroll = 0;
    if (strcmp(dir, "/")) {
        strcpy(entries[0].name, "..");
        entries[0].is_dir = 1;
        count = 1;
    }
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return;
    struct sys_dirent d;
    while (count < MAX_ENTRIES && readdir(fd, &d) > 0) {
        strncpy(entries[count].name, d.name, sizeof(entries[0].name) - 1);
        entries[count].name[sizeof(entries[0].name) - 1] = '\0';
        entries[count].is_dir = (int)d.is_dir;
        count++;
    }
    close(fd);
}

static void render(struct gwin *g) {
    gui_fill(g, 0, 0, W, H, RGB(30, 34, 50));
    gui_fill(g, 0, 0, W, 26, RGB(44, 52, 78));
    gui_text(g, 8, 9, dir, RGB(235, 240, 250), 1);
    for (int i = 0; i < VISIBLE && scroll + i < count; i++) {
        struct entry *e = &entries[scroll + i];
        int y = LIST_Y + i * ROW;
        char line[64];
        snprintf(line, sizeof(line), "%s %s", e->is_dir ? "[D]" : "   ", e->name);
        gui_text(g, 10, y, line, e->is_dir ? RGB(150, 200, 255) : RGB(235, 240, 250), 1);
    }
    if (count > VISIBLE) gui_text(g, W - 150, 9, "teclas j/k: rolar", RGB(150, 170, 200), 1);
    gui_present(g);
}

int main(int argc, char **argv) {
    if (argc > 1) strncpy(dir, argv[1], sizeof(dir) - 1);
    load();

    struct gwin g;
    if (gui_open(&g, W, H, "Arquivos") < 0) return 1;
    render(&g);

    struct win_event ev;
    while (gui_wait(&g, &ev) > 0) {
        if (ev.type == WIN_EV_CLOSE) return 0;
        if (ev.type == WIN_EV_KEY) {
            if (ev.a == 'j' && scroll + VISIBLE < count) scroll++;
            if (ev.a == 'k' && scroll > 0) scroll--;
        } else if (ev.type == WIN_EV_MOUSE && ev.c == 1 && ev.b >= LIST_Y) {
            int i = scroll + (ev.b - LIST_Y) / ROW;
            if (i < count) {
                struct entry *e = &entries[i];
                char path[128];
                if (!strcmp(e->name, "..")) {
                    char *slash = strrchr(dir, '/');
                    if (slash == dir) dir[1] = '\0'; else if (slash) *slash = '\0';
                    load();
                } else if (e->is_dir) {
                    join(path, sizeof(path), dir, e->name);
                    strncpy(dir, path, sizeof(dir) - 1);
                    load();
                } else {
                    join(path, sizeof(path), dir, e->name);
                    char *args[] = { "notepad", path, NULL };
                    spawn("/bin/notepad", args);
                }
            }
        }
        render(&g);
    }
    return 0;
}
