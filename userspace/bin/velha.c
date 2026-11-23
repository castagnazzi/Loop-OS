/* velha: tic-tac-toe on a 3D board (software rendered, no GPU).
 *
 * Click a cell to play (you are X). Left/Right arrows or A/D turn the board, Up/Down tilt it.
 * Modes: 2 players, easy machine (random, takes a win if it sees one), hard machine (perfect
 * minimax: it never loses). N restarts. */

#include <gui.h>

#define W 480
#define HEAD 64
#define VIEW 420
#define H (HEAD + VIEW)
#define MAXF 700

enum { M_PVP, M_EASY, M_HARD };

static int board[9];                    /* 0 empty, 1 X, 2 O */
static int turn = 1, over, win_line = -1, mode = M_HARD;
static int yaw = 20, pitch = 52;
static unsigned long think_until = 0, seed = 7;
static int wins_x, wins_o, draws;

static const int LINES[8][3] = { {0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6} };

/* ---- integer trigonometry: result scaled by 1024 ---- */
static int isin(int deg) {
    deg %= 360;
    if (deg < 0) deg += 360;
    int sign = 1;
    if (deg >= 180) { deg -= 180; sign = -1; }
    int num = 4 * deg * (180 - deg) * 1024;
    int den = 40500 - deg * (180 - deg);
    return sign * (num / den);
}
static int icos(int deg) { return isin(deg + 90); }

/* ---- projection ---- */
struct pt { int x, y; };
static int cam_d = 2300, focal = 880;

static struct pt project(int x, int y, int z) {
    int c = icos(yaw), s = isin(yaw);
    int x1 = (x * c - z * s) / 1024;
    int z1 = (x * s + z * c) / 1024;
    int cp = icos(pitch), sp = isin(pitch);
    int y2 = (y * cp + z1 * sp) / 1024;
    int z2 = (z1 * cp - y * sp) / 1024;
    int depth = cam_d + z2;
    if (depth < 200) depth = 200;
    struct pt p = { W / 2 + x1 * focal / depth, HEAD + VIEW / 2 + 20 - y2 * focal / depth };
    return p;
}

static int depth_of(int x, int y, int z) {
    int c = icos(yaw), s = isin(yaw);
    int z1 = (x * s + z * c) / 1024;
    int cp = icos(pitch), sp = isin(pitch);
    return (z1 * cp - y * sp) / 1024;
}

/* ---- face list (painter's algorithm) ---- */
struct face { struct pt p[4]; uint32_t color; int depth; int cell; };
static struct face faces[MAXF];
static int nfaces;
static struct pt tops[9][4];

static uint32_t shade(uint32_t c, int pct) {
    int r = ((c >> 16) & 255) * pct / 100, g = ((c >> 8) & 255) * pct / 100, b = (c & 255) * pct / 100;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB(r, g, b);
}

static void add_box(int cx, int cz, int y0, int y1, int hx, int hz, int rot, uint32_t color, int cell) {
    int cr = icos(rot), sr = isin(rot);
    int lx[4] = { -hx, hx, hx, -hx }, lz[4] = { -hz, -hz, hz, hz };
    int wx[4], wz[4];
    for (int i = 0; i < 4; i++) {
        wx[i] = cx + (lx[i] * cr - lz[i] * sr) / 1024;
        wz[i] = cz + (lx[i] * sr + lz[i] * cr) / 1024;
    }
    if (nfaces + 5 > MAXF) return;
    struct face *t = &faces[nfaces++];                      /* top */
    for (int i = 0; i < 4; i++) t->p[i] = project(wx[i], y1, wz[i]);
    t->color = color;
    t->depth = depth_of(cx, y1, cz);
    t->cell = cell;
    for (int i = 0; i < 4; i++) {                           /* four sides */
        int j = (i + 1) & 3;
        struct face *f = &faces[nfaces++];
        f->p[0] = project(wx[i], y1, wz[i]);
        f->p[1] = project(wx[j], y1, wz[j]);
        f->p[2] = project(wx[j], y0, wz[j]);
        f->p[3] = project(wx[i], y0, wz[i]);
        int mx = (wx[i] + wx[j]) / 2, mz = (wz[i] + wz[j]) / 2;
        f->depth = depth_of(mx, (y0 + y1) / 2, mz);
        f->cell = -1;
        /* hide sides that face away from the camera: compare with the box centre */
        int dc = depth_of(cx, (y0 + y1) / 2, cz);
        if (f->depth >= dc) { nfaces--; continue; }
        int ang = rot + yaw + i * 90 + 45;                  /* rough outward direction on screen */
        int light = 62 + (icos(ang - 225) * 22) / 1024;
        f->color = shade(color, light);
    }
}

static void fill_poly(struct gwin *g, const struct pt *p, int n, uint32_t color) {
    int ymin = p[0].y, ymax = p[0].y;
    for (int i = 1; i < n; i++) { if (p[i].y < ymin) ymin = p[i].y; if (p[i].y > ymax) ymax = p[i].y; }
    if (ymin < HEAD) ymin = HEAD;
    if (ymax >= H) ymax = H - 1;
    for (int y = ymin; y <= ymax; y++) {
        int xl = 100000, xr = -100000;
        for (int i = 0; i < n; i++) {
            struct pt a = p[i], b = p[(i + 1) % n];
            if (a.y == b.y) { if (a.y == y) { if (a.x < xl) xl = a.x; if (a.x > xr) xr = a.x; if (b.x < xl) xl = b.x; if (b.x > xr) xr = b.x; } continue; }
            if ((y < a.y && y < b.y) || (y > a.y && y > b.y)) continue;
            int x = a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (x < xl) xl = x;
            if (x > xr) xr = x;
        }
        if (xl < 0) xl = 0;
        if (xr >= W) xr = W - 1;
        if (xr >= xl) gui_fill(g, xl, y, xr - xl + 1, 1, color);
    }
}

static int inside_quad(const struct pt *q, int px, int py) {
    int pos = 0, neg = 0;
    for (int i = 0; i < 4; i++) {
        struct pt a = q[i], b = q[(i + 1) & 3];
        long cr = (long)(b.x - a.x) * (py - a.y) - (long)(b.y - a.y) * (px - a.x);
        if (cr > 0) pos = 1;
        if (cr < 0) neg = 1;
    }
    return !(pos && neg);
}

/* ---- game logic ---- */
static unsigned rnd(void) { seed = seed * 1103515245UL + 12345UL; return (unsigned)(seed >> 16) & 0x7FFF; }

static int winner_of(const int *b) {
    for (int i = 0; i < 8; i++)
        if (b[LINES[i][0]] && b[LINES[i][0]] == b[LINES[i][1]] && b[LINES[i][0]] == b[LINES[i][2]]) return b[LINES[i][0]];
    return 0;
}
static int is_full(const int *b) { for (int i = 0; i < 9; i++) if (!b[i]) return 0; return 1; }

static int minimax(int *b, int o_turn, int depth) {
    int w = winner_of(b);
    if (w == 2) return 10 - depth;
    if (w == 1) return depth - 10;
    if (is_full(b)) return 0;
    int best = o_turn ? -100 : 100;
    for (int i = 0; i < 9; i++) {
        if (b[i]) continue;
        b[i] = o_turn ? 2 : 1;
        int s = minimax(b, !o_turn, depth + 1);
        b[i] = 0;
        if (o_turn ? s > best : s < best) best = s;
    }
    return best;
}

static void check_end(void) {
    int w = winner_of(board);
    if (w) {
        over = w;
        if (w == 1) wins_x++; else wins_o++;
        for (int i = 0; i < 8; i++)
            if (board[LINES[i][0]] == w && board[LINES[i][1]] == w && board[LINES[i][2]] == w) win_line = i;
    } else if (is_full(board)) {
        over = 3;
        draws++;
    }
}

static void machine_move(void) {
    int move = -1;
    if (mode == M_HARD) {
        int best = -1000;
        for (int i = 0; i < 9; i++) {
            if (board[i]) continue;
            board[i] = 2;
            int s = minimax(board, 0, 1);
            board[i] = 0;
            if (s > best) { best = s; move = i; }
        }
    } else {
        for (int i = 0; i < 9 && move < 0; i++) {              /* take a win when there is one */
            if (board[i]) continue;
            board[i] = 2;
            if (winner_of(board) == 2) move = i;
            board[i] = 0;
        }
        while (move < 0) { int i = (int)(rnd() % 9); if (!board[i]) move = i; }
    }
    if (move >= 0) board[move] = 2;
    turn = 1;
    check_end();
}

static void play(int i) {
    if (over || board[i]) return;
    if (mode != M_PVP && turn != 1) return;
    board[i] = turn;
    check_end();
    if (over) return;
    if (mode == M_PVP) turn = turn == 1 ? 2 : 1;
    else { turn = 2; think_until = uptime_ms() + 350; }
}

static void reset(void) {
    for (int i = 0; i < 9; i++) board[i] = 0;
    turn = 1; over = 0; win_line = -1; think_until = 0;
}

/* ---- rendering ---- */
static void build_scene(void) {
    nfaces = 0;
    const int pitch_cell = 270, half = 122;
    for (int i = 0; i < 9; i++) {
        int cx = (i % 3 - 1) * pitch_cell, cz = (i / 3 - 1) * pitch_cell;
        uint32_t base = RGB(48, 54, 104);
        if (win_line >= 0 && (LINES[win_line][0] == i || LINES[win_line][1] == i || LINES[win_line][2] == i)) base = RGB(255, 235, 59);
        int before = nfaces;
        add_box(cx, cz, 0, 42, half, half, 0, base, i);
        for (int k = before; k < nfaces; k++) if (faces[k].cell == i) { for (int q = 0; q < 4; q++) tops[i][q] = faces[k].p[q]; }
        if (board[i] == 1) {
            add_box(cx, cz, 42, 120, 24, 100, 45, RGB(79, 195, 247), -1);
            add_box(cx, cz, 42, 120, 24, 100, -45, RGB(79, 195, 247), -1);
        } else if (board[i] == 2) {
            for (int k = 0; k < 12; k++) {
                int a = k * 30;
                int px = cx + isin(a + 90) * 82 / 1024, pz = cz + isin(a) * 82 / 1024;
                add_box(px, pz, 42, 120, 22, 30, a, RGB(255, 112, 67), -1);
            }
        }
    }
    for (int i = 1; i < nfaces; i++) {                       /* far to near */
        struct face f = faces[i];
        int j = i - 1;
        while (j >= 0 && faces[j].depth < f.depth) { faces[j + 1] = faces[j]; j--; }
        faces[j + 1] = f;
    }
}

static void render(struct gwin *g) {
    gui_fill(g, 0, 0, W, H, RGB(12, 12, 29));
    build_scene();
    gui_fill(g, 0, HEAD, W, VIEW, RGB(12, 12, 29));
    for (int i = 0; i < nfaces; i++) fill_poly(g, faces[i].p, 4, faces[i].color);

    /* header */
    gui_fill(g, 0, 0, W, HEAD, RGB(22, 24, 46));
    const char *msg;
    uint32_t mc = RGB(225, 230, 245);
    if (over == 1) { msg = mode == M_PVP ? "X venceu!" : "Voce venceu!"; mc = RGB(120, 230, 140); }
    else if (over == 2) { msg = mode == M_PVP ? "O venceu!" : "A maquina venceu"; mc = RGB(255, 130, 110); }
    else if (over == 3) { msg = "Empate"; mc = RGB(240, 220, 120); }
    else if (mode != M_PVP && turn == 2) msg = "Maquina pensando...";
    else msg = turn == 1 ? "Vez do X" : "Vez do O";
    gui_text(g, 12, 8, msg, mc, 1);
    char b[64];
    snprintf(b, sizeof(b), "X %d   O %d   Empates %d", wins_x, wins_o, draws);
    gui_text(g, W - 8 * (int)strlen(b) - 12, 8, b, RGB(150, 160, 190), 1);
    static const char *names[3] = { "2 Jogadores", "Facil", "Dificil" };
    for (int i = 0; i < 3; i++) gui_button(g, 12 + i * 112, 32, i == 0 ? 104 : 72, 24, names[i], mode == i);
    gui_button(g, W - 112, 32, 100, 24, "Novo (N)", 0);
    gui_present(g);
}

int main(void) {
    struct gwin g;
    if (gui_open(&g, W, H, "Jogo da Velha 3D") < 0) return 1;
    seed ^= uptime_ms();
    render(&g);

    unsigned long last = 0;
    struct win_event ev;
    for (;;) {
        int dirty = 0, r;
        while ((r = gui_poll(&g, &ev)) > 0) {
            if (ev.type == WIN_EV_CLOSE) return 0;
            if (ev.type == WIN_EV_KEY) {
                unsigned char c = (unsigned char)ev.a;
                if (c == 0x13 || c == 'a' || c == 'A') yaw -= 12;
                else if (c == 0x14 || c == 'd' || c == 'D') yaw += 12;
                else if (c == 0x11 || c == 'w' || c == 'W') { if (pitch < 80) pitch += 5; }
                else if (c == 0x12 || c == 's' || c == 'S') { if (pitch > 25) pitch -= 5; }
                else if (c == 'n' || c == 'N' || c == ' ') reset();
                else if (c >= '1' && c <= '3') { mode = c - '1'; reset(); }
                dirty = 1;
            } else if (ev.type == WIN_EV_MOUSE && ev.c == 1) {
                for (int i = 0; i < 3; i++)
                    if (gui_inside(ev.a, ev.b, 12 + i * 112, 32, i == 0 ? 104 : 72, 24)) { mode = i; reset(); }
                if (gui_inside(ev.a, ev.b, W - 112, 32, 100, 24)) reset();
                if (ev.b >= HEAD) {
                    for (int i = 0; i < 9; i++) if (inside_quad(tops[i], ev.a, ev.b)) { play(i); break; }
                }
                dirty = 1;
            }
        }
        if (r < 0) return 0;
        if (!over && mode != M_PVP && turn == 2 && uptime_ms() >= think_until) { machine_move(); dirty = 1; }
        unsigned long now = uptime_ms();
        if (dirty || (now - last > 1000)) { last = now; render(&g); }
        else sleep_ms(15);
    }
}
