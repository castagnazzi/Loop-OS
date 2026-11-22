/* calc: a four-function integer calculator (mouse or keyboard) */

#include <gui.h>

#define W 232
#define H 346
#define BTN 52
#define GAP 6
#define LEFT 6
#define TOP 62

static const char *labels[5][4] = {
    { "7", "8", "9", "/" },
    { "4", "5", "6", "*" },
    { "1", "2", "3", "-" },
    { "0", "C", "=", "+" },
    { "<", "", "", "" },
};

static long acc = 0, cur = 0;
static char op = 0;
static int fresh = 1, error = 0;

static void apply(void) {
    if (!op) { acc = cur; return; }
    switch (op) {
    case '+': acc += cur; break;
    case '-': acc -= cur; break;
    case '*': acc *= cur; break;
    case '/': if (cur == 0) error = 1; else acc /= cur; break;
    }
}

static void press(char c) {
    if (error && c != 'C' && c != 'c') return;
    if (c >= '0' && c <= '9') {
        if (fresh) { cur = 0; fresh = 0; }
        if (cur < 100000000L) cur = cur * 10 + (c - '0');
    } else if (c == '+' || c == '-' || c == '*' || c == '/') {
        if (!fresh || !op) { apply(); cur = acc; }
        op = c;
        fresh = 1;
    } else if (c == '=' || c == '\n') {
        apply();
        cur = acc;
        op = 0;
        fresh = 1;
    } else if (c == 'C' || c == 'c') {
        acc = cur = 0; op = 0; fresh = 1; error = 0;
    } else if (c == '<' || c == '\b') {
        if (!fresh) cur /= 10;
    }
}

static void render(struct gwin *g) {
    gui_fill(g, 0, 0, W, H, RGB(30, 34, 50));
    gui_fill(g, LEFT, 8, W - 2 * LEFT, 44, RGB(14, 18, 30));
    char buf[32];
    if (error) snprintf(buf, sizeof(buf), "erro");
    else snprintf(buf, sizeof(buf), "%ld", cur);
    int w = (int)strlen(buf) * 16;
    gui_text(g, W - LEFT - 8 - w, 14, buf, RGB(150, 230, 180), 2);
    if (op) gui_char(g, LEFT + 6, 10, op, RGB(150, 170, 200), 1);

    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 4; c++) {
            if (!labels[r][c][0]) continue;
            gui_button(g, LEFT + c * (BTN + GAP), TOP + r * (BTN + GAP), BTN, BTN, labels[r][c], 0);
        }
    }
    gui_present(g);
}

int main(void) {
    struct gwin g;
    if (gui_open(&g, W, H, "Calculadora") < 0) return 1;
    render(&g);

    struct win_event ev;
    while (gui_wait(&g, &ev) > 0) {
        if (ev.type == WIN_EV_CLOSE) return 0;
        if (ev.type == WIN_EV_KEY) {
            press((char)ev.a);
        } else if (ev.type == WIN_EV_MOUSE && ev.c == 1) {
            for (int r = 0; r < 5; r++) {
                for (int c = 0; c < 4; c++) {
                    if (labels[r][c][0] &&
                        gui_inside(ev.a, ev.b, LEFT + c * (BTN + GAP), TOP + r * (BTN + GAP), BTN, BTN)) {
                        press(labels[r][c][0]);
                    }
                }
            }
        }
        render(&g);
    }
    return 0;
}
