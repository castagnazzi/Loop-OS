/* files: file explorer with a folder tree.
 *
 * Left: tree of folders ([+] expands, click selects). Right: contents of the
 * selected folder with sizes. Double-click opens a folder, runs a program
 * from /bin or /sbin, or opens any other file in the notepad.
 * Toolbar: Acima, Atualizar, Nova pasta, Excluir. Keys: j/k scroll the list,
 * Backspace goes up. */

#include <gui.h>

#define W 780
#define H 520
#define TREE_W 220
#define TOOL_H 32
#define ROW_H 20
#define STATUS_H 24
#define PANE_TOP (TOOL_H + 2)
#define LIST_X (TREE_W + 4)
#define LIST_HEAD_H 22

#define MAXN 256
#define MAXI 512

#define C_BG       RGB(26, 30, 44)
#define C_PANE     RGB(18, 22, 34)
#define C_TOOL     RGB(40, 48, 72)
#define C_TEXT     RGB(225, 232, 245)
#define C_DIM      RGB(140, 156, 186)
#define C_DIR      RGB(150, 205, 255)
#define C_SEL      RGB(52, 104, 190)
#define C_SEL_OFF  RGB(48, 60, 92)
#define C_ERR      RGB(255, 150, 130)

struct tn {
    char path[96];
    char name[40];
    int parent, depth, expanded, loaded;
};

struct item {
    char name[48];
    int is_dir;
    unsigned long size;
};

static struct tn nodes[MAXN];
static int nn = 0;
static int tree_sel = 0, tree_scroll = 0;
static int trows[MAXN], ntrows = 0;

static struct item items[MAXI];
static int ni = 0, list_sel = -1, list_scroll = 0;

static char cwd[96] = "/";
static char message[64] = "";
static int message_err = 0;

static struct gwin g;

/* ---------------------------------------------------------------- */
static void join(char *out, size_t max, const char *base, const char *name) {
    snprintf(out, max, "%s%s%s", base, base[strlen(base) - 1] == '/' ? "" : "/", name);
}

static void parent_of(const char *path, char *out, size_t max) {
    strncpy(out, path, max - 1);
    out[max - 1] = '\0';
    char *slash = strrchr(out, '/');
    if (!slash || slash == out) strcpy(out, "/");
    else *slash = '\0';
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int name_cmp(const char *a, const char *b) {
    for (int i = 0;; i++) {
        char x = lower(a[i]), y = lower(b[i]);
        if (x != y) return (unsigned char)x - (unsigned char)y;
        if (!x) return 0;
    }
}

static int cmp_items(const struct item *a, const struct item *b) {
    if (a->is_dir != b->is_dir) return b->is_dir - a->is_dir;
    return name_cmp(a->name, b->name);
}

static void set_message(const char *text, int err) {
    strncpy(message, text, sizeof(message) - 1);
    message[sizeof(message) - 1] = '\0';
    message_err = err;
}

/* ---------------------------------------------------------------- */
/* Tree */
static int add_node(const char *path, const char *name, int parent, int depth) {
    if (nn >= MAXN) return -1;
    struct tn *t = &nodes[nn];
    memset(t, 0, sizeof(*t));
    strncpy(t->path, path, sizeof(t->path) - 1);
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->parent = parent;
    t->depth = depth;
    return nn++;
}

static void load_children(int i) {
    struct tn *t = &nodes[i];
    if (t->loaded) return;
    t->loaded = 1;
    int fd = open(t->path, O_RDONLY);
    if (fd < 0) return;
    /* collect the sub-folder names, sort them, then add them in order */
    static char names[MAXN][40];
    int count = 0;
    struct sys_dirent d;
    while (count < MAXN && readdir(fd, &d) > 0) {
        if (!d.is_dir) continue;
        int k = count++;
        strncpy(names[k], d.name, sizeof(names[k]) - 1);
        names[k][sizeof(names[k]) - 1] = '\0';
        while (k > 0 && name_cmp(names[k - 1], names[k]) > 0) {
            char tmp[40];
            strcpy(tmp, names[k - 1]);
            strcpy(names[k - 1], names[k]);
            strcpy(names[k], tmp);
            k--;
        }
    }
    close(fd);
    for (int k = 0; k < count; k++) {
        char path[96];
        join(path, sizeof(path), t->path, names[k]);
        if (add_node(path, names[k], i, t->depth + 1) < 0) break;
    }
}

static int find_child(int parent, const char *name, size_t len) {
    for (int j = 0; j < nn; j++) {
        if (nodes[j].parent == parent && j != parent && strlen(nodes[j].name) == len &&
            !strncmp(nodes[j].name, name, len)) return j;
    }
    return -1;
}

/* Expand the tree down to 'path' and select it */
static void reveal(const char *path) {
    int cur = 0;
    nodes[0].expanded = 1;
    const char *p = path;
    while (*p == '/') p++;
    while (*p) {
        const char *s = p;
        while (*p && *p != '/') p++;
        size_t len = (size_t)(p - s);
        while (*p == '/') p++;
        load_children(cur);
        int c = find_child(cur, s, len);
        if (c < 0) break;
        nodes[cur].expanded = 1;
        cur = c;
    }
    tree_sel = cur;
}

static void tree_visit(int i) {
    if (ntrows < MAXN) trows[ntrows++] = i;
    if (!nodes[i].expanded) return;
    load_children(i);
    for (int j = 0; j < nn; j++) {
        if (nodes[j].parent == i && j != i) tree_visit(j);
    }
}

static void build_rows(void) {
    ntrows = 0;
    tree_visit(0);
}

/* ---------------------------------------------------------------- */
/* Folder contents */
static void load_items(void) {
    ni = 0;
    list_sel = -1;
    list_scroll = 0;
    int fd = open(cwd, O_RDONLY);
    if (fd < 0) return;
    struct sys_dirent d;
    while (ni < MAXI && readdir(fd, &d) > 0) {
        struct item *it = &items[ni];
        strncpy(it->name, d.name, sizeof(it->name) - 1);
        it->name[sizeof(it->name) - 1] = '\0';
        it->is_dir = (int)d.is_dir;
        it->size = (unsigned long)d.size;
        /* insertion sort */
        int k = ni++;
        while (k > 0 && cmp_items(&items[k - 1], &items[k]) > 0) {
            struct item tmp = items[k - 1];
            items[k - 1] = items[k];
            items[k] = tmp;
            k--;
        }
    }
    close(fd);
}

static void navigate(const char *path) {
    strncpy(cwd, path, sizeof(cwd) - 1);
    cwd[sizeof(cwd) - 1] = '\0';
    reveal(cwd);
    build_rows();
    load_items();
    set_message("", 0);
}

static void rebuild(void) {
    nn = 0;
    add_node("/", "/", -1, 0);
    nodes[0].expanded = 1;
    reveal(cwd);
    build_rows();
    load_items();
}

static void fmt_size(unsigned long n, char *out, size_t max) {
    if (n < 1024) snprintf(out, max, "%lu B", n);
    else if (n < 1024UL * 1024) snprintf(out, max, "%lu KB", n / 1024);
    else snprintf(out, max, "%lu.%lu MB", n / (1024UL * 1024), (n * 10 / (1024UL * 1024)) % 10);
}

static int list_visible(void) { return (H - STATUS_H - (PANE_TOP + LIST_HEAD_H)) / ROW_H; }
static int tree_visible(void) { return (H - STATUS_H - PANE_TOP - 4) / ROW_H; }

/* ---------------------------------------------------------------- */
static void open_item(int idx) {
    struct item *it = &items[idx];
    char path[128];
    join(path, sizeof(path), cwd, it->name);
    if (it->is_dir) {
        navigate(path);
        return;
    }
    if (!strncmp(path, "/bin/", 5) || !strncmp(path, "/sbin/", 6)) {
        char *args[] = { it->name, NULL };
        if (spawn(path, args) < 0) set_message("nao foi possivel executar", 1);
        return;
    }
    char *args[] = { "notepad", path, NULL };
    if (spawn("/bin/notepad", args) < 0) set_message("nao foi possivel abrir", 1);
}

static void go_up(void) {
    if (!strcmp(cwd, "/")) return;
    char up[96];
    parent_of(cwd, up, sizeof(up));
    navigate(up);
}

static void new_folder(void) {
    for (int n = 1; n < 100; n++) {
        char name[48], path[128];
        if (n == 1) snprintf(name, sizeof(name), "Nova pasta");
        else snprintf(name, sizeof(name), "Nova pasta %d", n);
        join(path, sizeof(path), cwd, name);
        int fd = open(path, O_RDONLY);
        if (fd >= 0) { close(fd); continue; }
        if (mkdir(path) < 0) {
            set_message("nao e possivel criar aqui (somente leitura)", 1);
            return;
        }
        rebuild();
        set_message("pasta criada", 0);
        return;
    }
}

static void delete_selected(void) {
    if (list_sel < 0 || list_sel >= ni) {
        set_message("selecione um item", 1);
        return;
    }
    char path[128];
    join(path, sizeof(path), cwd, items[list_sel].name);
    int r = unlink(path);
    if (r == 0) {
        rebuild();
        set_message("excluido", 0);
    } else if (r == -39) {
        set_message("a pasta nao esta vazia", 1);
    } else if (r == -1) {
        set_message("arquivo do sistema: protegido", 1);
    } else if (r == -30) {
        set_message("somente leitura", 1);
    } else {
        set_message("nao foi possivel excluir", 1);
    }
}

/* ---------------------------------------------------------------- */
static void draw_arrow(int x, int y, const char *label) {
    gui_fill(&g, x, y, 18, 18, C_TOOL);
    gui_rect(&g, x, y, 18, 18, RGB(100, 118, 160));
    gui_text(&g, x + 5, y + 1, label, C_TEXT, 1);
}

static void render(void) {
    gui_fill(&g, 0, 0, W, H, C_BG);

    /* toolbar */
    gui_fill(&g, 0, 0, W, TOOL_H, C_TOOL);
    gui_button(&g, 4, 4, 64, 24, "Acima", 0);
    gui_button(&g, 72, 4, 90, 24, "Atualizar", 0);
    gui_button(&g, 166, 4, 104, 24, "Nova pasta", 0);
    gui_button(&g, 274, 4, 72, 24, "Excluir", 0);
    gui_fill(&g, 356, 4, W - 360, 24, C_PANE);
    gui_text(&g, 362, 8, cwd, C_TEXT, 1);

    /* tree */
    gui_fill(&g, 0, PANE_TOP, TREE_W, H - STATUS_H - PANE_TOP, C_PANE);
    int tv = tree_visible();
    for (int r = 0; r < tv && tree_scroll + r < ntrows; r++) {
        struct tn *t = &nodes[trows[tree_scroll + r]];
        int y = PANE_TOP + 2 + r * ROW_H;
        if (trows[tree_scroll + r] == tree_sel) gui_fill(&g, 0, y, TREE_W - 20, ROW_H, C_SEL);
        int x = 6 + t->depth * 14;
        gui_text(&g, x, y + 2, t->expanded ? "-" : "+", C_DIM, 1);
        gui_text(&g, x + 12, y + 2, t->name, C_DIR, 1);
    }
    draw_arrow(TREE_W - 19, PANE_TOP + 1, "^");
    draw_arrow(TREE_W - 19, H - STATUS_H - 19, "v");
    gui_fill(&g, TREE_W, PANE_TOP, 4, H - STATUS_H - PANE_TOP, C_BG);

    /* list */
    int lx = LIST_X, lw = W - LIST_X;
    gui_fill(&g, lx, PANE_TOP, lw, H - STATUS_H - PANE_TOP, C_PANE);
    gui_fill(&g, lx, PANE_TOP, lw, LIST_HEAD_H, C_TOOL);
    gui_text(&g, lx + 8, PANE_TOP + 3, "Nome", C_DIM, 1);
    gui_text(&g, W - 110, PANE_TOP + 3, "Tamanho", C_DIM, 1);
    int lv = list_visible();
    for (int r = 0; r < lv && list_scroll + r < ni; r++) {
        int idx = list_scroll + r;
        struct item *it = &items[idx];
        int y = PANE_TOP + LIST_HEAD_H + r * ROW_H;
        if (idx == list_sel) gui_fill(&g, lx, y, lw - 20, ROW_H, C_SEL);
        gui_text(&g, lx + 8, y + 2, it->is_dir ? "[ ]" : "   ", C_DIM, 1);
        char shown[48];
        strncpy(shown, it->name, sizeof(shown) - 1);
        shown[sizeof(shown) - 1] = '\0';
        gui_text(&g, lx + 36, y + 2, shown, it->is_dir ? C_DIR : C_TEXT, 1);
        if (!it->is_dir) {
            char sz[24];
            fmt_size(it->size, sz, sizeof(sz));
            gui_text(&g, W - 28 - 8 * (int)strlen(sz), y + 2, sz, C_DIM, 1);
        }
    }
    draw_arrow(W - 19, PANE_TOP + LIST_HEAD_H + 1, "^");
    draw_arrow(W - 19, H - STATUS_H - 19, "v");

    /* status */
    gui_fill(&g, 0, H - STATUS_H, W, STATUS_H, C_TOOL);
    char st[96];
    if (message[0]) snprintf(st, sizeof(st), "%s", message);
    else if (list_sel >= 0 && list_sel < ni && !items[list_sel].is_dir) {
        char sz[24];
        fmt_size(items[list_sel].size, sz, sizeof(sz));
        snprintf(st, sizeof(st), "%s - %s", items[list_sel].name, sz);
    } else snprintf(st, sizeof(st), "%d itens", ni);
    gui_text(&g, 8, H - STATUS_H + 4, st, message_err ? C_ERR : C_TEXT, 1);
    gui_present(&g);
}

/* ---------------------------------------------------------------- */
static void click(int x, int y) {
    static unsigned long last_time = 0;
    static int last_idx = -1;
    set_message("", 0);

    if (y < TOOL_H) {
        if (gui_inside(x, y, 4, 4, 64, 24)) go_up();
        else if (gui_inside(x, y, 72, 4, 90, 24)) { rebuild(); set_message("atualizado", 0); }
        else if (gui_inside(x, y, 166, 4, 104, 24)) new_folder();
        else if (gui_inside(x, y, 274, 4, 72, 24)) delete_selected();
        return;
    }

    if (x < TREE_W) {
        if (gui_inside(x, y, TREE_W - 19, PANE_TOP + 1, 18, 18)) { if (tree_scroll > 0) tree_scroll--; return; }
        if (gui_inside(x, y, TREE_W - 19, H - STATUS_H - 19, 18, 18)) {
            if (tree_scroll + tree_visible() < ntrows) tree_scroll++;
            return;
        }
        int r = (y - (PANE_TOP + 2)) / ROW_H;
        if (y >= PANE_TOP + 2 && tree_scroll + r < ntrows && r < tree_visible()) {
            int ni_ = trows[tree_scroll + r];
            struct tn *t = &nodes[ni_];
            int mx = 6 + t->depth * 14;
            if (x >= mx && x < mx + 12) {
                t->expanded = !t->expanded;
                build_rows();
            } else {
                navigate(t->path);
            }
        }
        return;
    }

    if (x < LIST_X) return;
    if (gui_inside(x, y, W - 19, PANE_TOP + LIST_HEAD_H + 1, 18, 18)) { if (list_scroll > 0) list_scroll--; return; }
    if (gui_inside(x, y, W - 19, H - STATUS_H - 19, 18, 18)) {
        if (list_scroll + list_visible() < ni) list_scroll++;
        return;
    }
    int top = PANE_TOP + LIST_HEAD_H;
    if (y >= top) {
        int r = (y - top) / ROW_H;
        int idx = list_scroll + r;
        if (r < list_visible() && idx < ni) {
            unsigned long now = uptime_ms();
            if (idx == last_idx && now - last_time < 600) {
                last_idx = -1;
                open_item(idx);
                return;
            }
            last_idx = idx;
            last_time = now;
            list_sel = idx;
        }
    }
}

/* Returns 1 when the window should close */
static int handle_event(const struct win_event *ev, int *was_down) {
    if (ev->type == WIN_EV_CLOSE) return 1;
    if (ev->type == WIN_EV_KEY) {
        if (ev->a == 'j' && list_scroll + list_visible() < ni) list_scroll++;
        else if (ev->a == 'k' && list_scroll > 0) list_scroll--;
        else if (ev->a == '\b' || ev->a == 127) go_up();
    } else if (ev->type == WIN_EV_MOUSE) {
        int down = ev->c == 1;
        if (down && !*was_down) click(ev->a, ev->b);
        *was_down = down;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1) strncpy(cwd, argv[1], sizeof(cwd) - 1);
    if (gui_open(&g, W, H, "Arquivos") < 0) return 1;
    rebuild();
    render();

    int was_down = 0;
    struct win_event ev;
    while (gui_wait(&g, &ev) > 0) {
        /* handle everything that is queued, then redraw once */
        do {
            if (handle_event(&ev, &was_down)) return 0;
        } while (gui_poll(&g, &ev) > 0);
        render();
    }
    return 0;
}
