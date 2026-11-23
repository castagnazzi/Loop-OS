/* cobrinha: native version of the snake game.
 *
 * Arrow keys / WASD steer, P pauses, 1-3 pick the speed, Space starts. Three kinds of food
 * (apple 10, star 25, diamond 50), particles, screen shake and a top-10 ranking kept in
 * /tmp/cobrinha.rec (it lives in RAM, so it is lost when the system restarts). */

#include <gui.h>

#define GRID 20
#define TILE 20
#define HEAD_H 58
#define W (GRID * TILE)
#define H (HEAD_H + GRID * TILE + 22)
#define RECORDS 10
#define MAXP 160

enum { S_START, S_PLAY, S_OVER };
enum { F_NORMAL, F_BONUS, F_POWER };

struct rec { char name[20]; int score; };
struct part { int x, y, vx, vy, size; int life, maxlife; uint32_t color; };   /* positions in 1/16 pixel */

static struct rec recs[RECORDS];
static int nrecs = 0;
static int sx[GRID * GRID], sy[GRID * GRID], len;
static int dir, ndir, fx, fy, ftype, ftimer, score, state, paused, speed = 1, frame;
static int entering, newrecord;
static char name[20];
static int name_len;
static int shake;                        /* in 1/16 pixel */
static struct part parts[MAXP];
static int nparts;
static unsigned long seed = 12345;
static const int SPEEDS[3] = { 150, 100, 60 };
static const int DX[4] = { 0, 1, 0, -1 }, DY[4] = { -1, 0, 1, 0 };     /* up right down left */

static unsigned rnd(void) {
    seed = seed * 1103515245UL + 12345UL;
    return (unsigned)(seed >> 16) & 0x7FFF;
}

/* ---- drawing helpers with alpha ---- */
static void blend(struct gwin *g, int x, int y, int w, int h, uint32_t c, int a) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g->w) w = g->w - x;
    if (y + h > g->h) h = g->h - y;
    int cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255;
    for (int j = 0; j < h; j++) {
        uint32_t *row = g->px + (size_t)(y + j) * g->w + x;
        for (int i = 0; i < w; i++) {
            uint32_t p = row[i];
            int r = (((p >> 16) & 255) * (255 - a) + cr * a) / 255;
            int gg = (((p >> 8) & 255) * (255 - a) + cg * a) / 255;
            int b = ((p & 255) * (255 - a) + cb * a) / 255;
            row[i] = RGB(r, gg, b);
        }
    }
}

static void disc(struct gwin *g, int cx, int cy, int r, uint32_t c, int a) {
    for (int j = -r; j <= r; j++) {
        int w = 0;
        while ((w + 1) * (w + 1) + j * j <= r * r) w++;
        if (r * r >= j * j) blend(g, cx - w, cy + j, 2 * w + 1, 1, c, a);
    }
}

static void rrect(struct gwin *g, int x, int y, int w, int h, int r, uint32_t c) {
    gui_fill(g, x + r, y, w - 2 * r, h, c);
    gui_fill(g, x, y + r, w, h - 2 * r, c);
    gui_fill(g, x + 1, y + 1, w - 2, h - 2, c);
}

/* ---- ranking ---- */
static void load_recs(void) {
    nrecs = 0;
    int fd = open("/tmp/cobrinha.rec", O_RDONLY);
    if (fd < 0) return;
    char buf[512];
    long n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';
    char *p = buf;
    while (*p && nrecs < RECORDS) {
        int sc = atoi(p);
        while (*p && *p != ' ') p++;
        if (*p == ' ') p++;
        int k = 0;
        while (*p && *p != '\n' && k < 19) recs[nrecs].name[k++] = *p++;
        recs[nrecs].name[k] = '\0';
        recs[nrecs].score = sc;
        nrecs++;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

static void save_recs(void) {
    int fd = open("/tmp/cobrinha.rec", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return;
    char line[48];
    for (int i = 0; i < nrecs; i++) {
        int l = snprintf(line, sizeof(line), "%d %s\n", recs[i].score, recs[i].name);
        write(fd, line, (size_t)l);
    }
    close(fd);
}

static void add_record(const char *nm, int sc) {
    int pos = nrecs;
    while (pos > 0 && recs[pos - 1].score < sc) pos--;
    if (pos >= RECORDS) return;
    int last = nrecs < RECORDS ? nrecs : RECORDS - 1;
    for (int i = last; i > pos; i--) recs[i] = recs[i - 1];
    snprintf(recs[pos].name, sizeof(recs[pos].name), "%s", nm[0] ? nm : "Anonimo");
    recs[pos].score = sc;
    if (nrecs < RECORDS) nrecs++;
    save_recs();
}

static int top_score(void) { return nrecs ? recs[0].score : 0; }

/* ---- particles ---- */
static void emit(int x, int y, uint32_t color, int count, int speedv) {   /* x, y in pixels; speedv in px/frame */
    for (int i = 0; i < count && nparts < MAXP; i++) {
        struct part *p = &parts[nparts++];
        p->x = x * 16; p->y = y * 16; p->color = color;
        p->vx = ((int)(rnd() % 1000) - 500) * speedv * 16 / 1000;
        p->vy = ((int)(rnd() % 1000) - 500) * speedv * 16 / 1000;
        p->size = 24 + (int)(rnd() % 48);
        p->maxlife = p->life = 20 + (int)(rnd() % 25);
    }
}

static void burst(int x, int y) {
    static const uint32_t cols[4] = { RGB(0, 255, 136), RGB(0, 204, 255), RGB(255, 204, 0), RGB(255, 102, 170) };
    for (int i = 0; i < 20; i++) emit(x, y, cols[rnd() % 4], 1, 8);
}

static void update_parts(void) {
    for (int i = nparts - 1; i >= 0; i--) {
        struct part *p = &parts[i];
        p->x += p->vx; p->y += p->vy; p->vy += 1;
        p->size = p->size * 98 / 100;
        if (--p->life <= 0) parts[i] = parts[--nparts];
    }
}

/* ---- game ---- */
static void place_food(void) {
    for (int tries = 0; tries < 200; tries++) {
        fx = (int)(rnd() % GRID);
        fy = (int)(rnd() % GRID);
        int ok = 1;
        for (int i = 0; i < len; i++) if (sx[i] == fx && sy[i] == fy) { ok = 0; break; }
        if (ok) break;
    }
    ftimer++;
    if (ftimer > 8 && rnd() % 100 < 15) ftype = F_BONUS;
    else if (ftimer > 15 && rnd() % 100 < 8) ftype = F_POWER;
    else ftype = F_NORMAL;
}

static void new_game(void) {
    len = 3;
    for (int i = 0; i < len; i++) { sx[i] = GRID / 2 - i; sy[i] = GRID / 2; }
    dir = ndir = 1;
    score = 0; ftimer = 0; shake = 0; nparts = 0; paused = 0; frame = 0;
    state = S_PLAY;
    place_food();
}

static void game_over(void) {
    state = S_OVER;
    for (int i = 0; i < len; i += 2) burst(sx[i] * TILE + TILE / 2, HEAD_H + sy[i] * TILE + TILE / 2);
    shake = 10 * 16;
    newrecord = score > 0 && (nrecs < RECORDS || score > recs[nrecs - 1].score);
    entering = newrecord;
    name_len = 0;
    name[0] = '\0';
}

static void step(void) {
    if (state != S_PLAY || paused) return;
    frame++;
    dir = ndir;
    int nx = sx[0] + DX[dir], ny = sy[0] + DY[dir];
    if (nx < 0 || ny < 0 || nx >= GRID || ny >= GRID) { game_over(); return; }
    for (int i = 0; i < len; i++) if (sx[i] == nx && sy[i] == ny) { game_over(); return; }
    int grow = (nx == fx && ny == fy);
    if (grow) len++;
    for (int i = len - 1; i > 0; i--) { sx[i] = sx[i - 1]; sy[i] = sy[i - 1]; }
    sx[0] = nx; sy[0] = ny;
    if (grow) {
        int px = fx * TILE + TILE / 2, py = HEAD_H + fy * TILE + TILE / 2;
        if (ftype == F_NORMAL) { score += 10; emit(px, py, RGB(255, 68, 68), 15, 5); }
        else if (ftype == F_BONUS) { score += 25; burst(px, py); shake = 5 * 16; }
        else { score += 50; burst(px, py); shake = 8 * 16; }
        if (ftype != F_NORMAL) ftimer = 0;           /* the counter keeps growing until a special food appears */
        place_food();
    }
}

static void draw_food(struct gwin *g, int ox, int oy) {
    int cx = ox + fx * TILE + TILE / 2, cy = oy + HEAD_H + fy * TILE + TILE / 2;
    int pulse = 7 + (int)((frame % 12) < 6 ? (frame % 6) / 2 : (11 - frame % 12) / 2);
    if (ftype == F_NORMAL) {
        disc(g, cx, cy, pulse + 3, RGB(255, 68, 68), 40);
        disc(g, cx, cy, 7, RGB(235, 50, 50), 255);
        disc(g, cx - 2, cy - 2, 2, RGB(255, 170, 170), 255);
        gui_fill(g, cx, cy - 10, 2, 4, RGB(90, 200, 90));
    } else if (ftype == F_BONUS) {
        disc(g, cx, cy, pulse + 3, RGB(255, 204, 0), 40);
        for (int k = -8; k <= 8; k++) {                /* a four-point star */
            int w = 8 - (k < 0 ? -k : k);
            gui_fill(g, cx - w / 3, cy + k, 2 * (w / 3) + 1, 1, RGB(255, 210, 40));
            gui_fill(g, cx + k, cy - w / 3, 1, 2 * (w / 3) + 1, RGB(255, 210, 40));
        }
        disc(g, cx, cy, 3, RGB(255, 245, 160), 255);
    } else {
        disc(g, cx, cy, pulse + 3, RGB(0, 204, 255), 40);
        for (int k = -8; k <= 8; k++) {                /* diamond */
            int w = 8 - (k < 0 ? -k : k);
            gui_fill(g, cx - w, cy + k, 2 * w + 1, 1, RGB(60, 210, 255));
        }
        disc(g, cx - 2, cy - 2, 2, RGB(210, 245, 255), 255);
    }
}

static void render(struct gwin *g) {
    int ox = 0, oy = 0;
    if (shake > 5) {
        ox = ((int)(rnd() % 1000) - 500) * shake / 16000;
        oy = ((int)(rnd() % 1000) - 500) * shake / 16000;
    }
    gui_fill(g, 0, 0, W, H, RGB(14, 16, 32));

    /* header */
    char b[48];
    gui_text(g, 12, 6, "PONTOS", RGB(136, 136, 170), 1);
    snprintf(b, sizeof(b), "%d", score);
    gui_text(g, 12, 24, b, RGB(0, 255, 136), 2);
    gui_text(g, W - 12 - 8 * 7, 6, "RECORDE", RGB(136, 136, 170), 1);
    snprintf(b, sizeof(b), "%d", top_score());
    gui_text(g, W - 12 - 16 * (int)strlen(b), 24, b, RGB(255, 204, 0), 2);
    static const char *sp[3] = { "1 Lento", "2 Normal", "3 Rapido" };
    for (int i = 0; i < 3; i++) gui_button(g, 100 + i * 70, 8, 68, 20, sp[i], speed == i);
    gui_text(g, 100, 34, paused ? "PAUSADO (P)" : "P pausa", paused ? RGB(255, 204, 0) : RGB(100, 104, 140), 1);

    /* field */
    gui_fill(g, ox, oy + HEAD_H, W, GRID * TILE, RGB(10, 10, 26));
    for (int i = 0; i <= GRID; i++) {
        blend(g, ox + i * TILE, oy + HEAD_H, 1, GRID * TILE, RGB(255, 255, 255), 8);
        blend(g, ox, oy + HEAD_H + i * TILE, W, 1, RGB(255, 255, 255), 8);
    }
    if (state != S_START) {
        for (int i = len - 1; i >= 0; i--) {
            int x = ox + sx[i] * TILE + 1, y = oy + HEAD_H + sy[i] * TILE + 1;
            int gcol = 255 - 100 * i / len, bcol = 136 - 80 * i / len;
            uint32_t c = RGB(0, gcol, bcol);
            blend(g, x - 3, y - 3, TILE + 4, TILE + 4, RGB(0, 255, 136), i == 0 ? 40 : 18);
            rrect(g, x, y, TILE - 2, TILE - 2, i == 0 ? 5 : 3, c);
            if (i == 0) {                              /* eyes follow the heading */
                int e1x, e1y, e2x, e2y;
                int m1 = TILE * 35 / 100, m2 = TILE * 65 / 100;
                if (dir == 1) { e1x = e2x = m2; e1y = m1; e2y = m2; }
                else if (dir == 3) { e1x = e2x = m1; e1y = m1; e2y = m2; }
                else if (dir == 0) { e1x = m1; e2x = m2; e1y = e2y = m1; }
                else { e1x = m1; e2x = m2; e1y = e2y = m2; }
                int bx = ox + sx[0] * TILE, by = oy + HEAD_H + sy[0] * TILE;
                disc(g, bx + e1x, by + e1y, 3, RGB(255, 255, 255), 255);
                disc(g, bx + e2x, by + e2y, 3, RGB(255, 255, 255), 255);
                disc(g, bx + e1x, by + e1y, 1, RGB(0, 0, 0), 255);
                disc(g, bx + e2x, by + e2y, 1, RGB(0, 0, 0), 255);
            }
        }
        draw_food(g, ox, oy);
    }
    for (int i = 0; i < nparts; i++) {
        struct part *p = &parts[i];
        int a = 255 * p->life / p->maxlife;
        disc(g, ox + p->x / 16, oy + p->y / 16, (p->size + 8) / 16, p->color, a);
    }

    /* footer */
    gui_text(g, 8, H - 18, "Setas/WASD mover  Espaco: jogar", RGB(100, 104, 140), 1);

    /* overlays */
    if (state != S_PLAY) {
        blend(g, 0, HEAD_H, W, GRID * TILE, RGB(8, 8, 20), 190);
        if (state == S_START) {
            gui_text(g, W / 2 - 8 * 7 * 2 / 2, HEAD_H + 40, "COBRINHA", RGB(0, 255, 136), 2);
            gui_text(g, W / 2 - 8 * 14 / 2, HEAD_H + 90, "Espaco para jogar", RGB(170, 170, 204), 1);
        } else {
            gui_text(g, W / 2 - 8 * 9, HEAD_H + 24, "FIM DE JOGO", RGB(255, 102, 102), 2);
            snprintf(b, sizeof(b), "Pontuacao: %d", score);
            gui_text(g, W / 2 - 8 * (int)strlen(b) / 2, HEAD_H + 64, b, RGB(255, 204, 0), 1);
            if (entering) {
                gui_text(g, W / 2 - 8 * 6, HEAD_H + 88, "Novo recorde!", RGB(255, 204, 0), 1);
                gui_fill(g, W / 2 - 100, HEAD_H + 108, 200, 24, RGB(10, 14, 30));
                gui_rect(g, W / 2 - 100, HEAD_H + 108, 200, 24, RGB(0, 255, 136));
                snprintf(b, sizeof(b), "%s_", name);
                gui_text(g, W / 2 - 94, HEAD_H + 112, b, RGB(225, 225, 255), 1);
                gui_text(g, W / 2 - 8 * 11, HEAD_H + 138, "Digite o nome e Enter", RGB(150, 150, 190), 1);
            } else {
                gui_text(g, W / 2 - 8 * 10, HEAD_H + 92, "Espaco: jogar de novo", RGB(170, 170, 204), 1);
            }
        }
        gui_text(g, W / 2 - 8 * 6, HEAD_H + 168, "TOP 5", RGB(255, 204, 0), 1);
        for (int i = 0; i < 5 && i < nrecs; i++) {
            snprintf(b, sizeof(b), "%d. %-12s %5d", i + 1, recs[i].name, recs[i].score);
            gui_text(g, W / 2 - 8 * 11, HEAD_H + 190 + i * 18, b, i == 0 ? RGB(255, 204, 0) : RGB(190, 195, 225), 1);
        }
        if (nrecs == 0) gui_text(g, W / 2 - 8 * 9, HEAD_H + 190, "Nenhum recorde ainda", RGB(120, 124, 160), 1);
    }
    gui_present(g);
}

static void set_dir(int d) {
    if (state != S_PLAY || paused) return;
    if (((d + 2) & 3) != dir) ndir = d;
}

int main(void) {
    struct gwin g;
    if (gui_open(&g, W, H, "Cobrinha") < 0) return 1;
    seed ^= uptime_ms();
    load_recs();
    state = S_START;
    render(&g);

    unsigned long last_step = uptime_ms(), last_frame = 0;
    struct win_event ev;
    for (;;) {
        int r;
        while ((r = gui_poll(&g, &ev)) > 0) {
            if (ev.type == WIN_EV_CLOSE) return 0;
            if (ev.type == WIN_EV_MOUSE && ev.c == 1) {
                for (int i = 0; i < 3; i++)
                    if (gui_inside(ev.a, ev.b, 100 + i * 70, 8, 68, 20)) speed = i;
                continue;
            }
            if (ev.type != WIN_EV_KEY) continue;
            unsigned char c = (unsigned char)ev.a;
            if (state == S_OVER && entering) {
                if (c == '\n' || c == '\r') { add_record(name, score); entering = 0; }
                else if ((c == '\b' || c == 127) && name_len > 0) name[--name_len] = '\0';
                else if (c >= 32 && c != 127 && name_len < 16) { name[name_len++] = (char)c; name[name_len] = '\0'; }
                continue;
            }
            switch (c) {
            case 0x11: case 'w': case 'W': set_dir(0); break;
            case 0x14: case 'd': case 'D': set_dir(1); break;
            case 0x12: case 's': case 'S': set_dir(2); break;
            case 0x13: case 'a': case 'A': set_dir(3); break;
            case 'p': case 'P': if (state == S_PLAY) paused = !paused; break;
            case '1': speed = 0; break;
            case '2': speed = 1; break;
            case '3': speed = 2; break;
            case ' ': if (state != S_PLAY) { new_game(); last_step = uptime_ms(); } break;
            }
        }
        if (r < 0) return 0;

        unsigned long now = uptime_ms();
        int need = 0;
        if (state == S_PLAY && !paused && now - last_step >= (unsigned long)SPEEDS[speed]) {
            last_step = now;
            step();
            need = 1;
        }
        if (now - last_frame >= 33) {                 /* about 30 frames per second for the effects */
            last_frame = now;
            update_parts();
            if (shake > 0) { shake = shake * 85 / 100; if (shake < 5) shake = 0; }
            frame++;
            need = 1;
        }
        if (need) render(&g);
        else sleep_ms(5);
    }
}
