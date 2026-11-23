/* navegador: a small web browser.
 *
 * Shows HTML pages from http:// servers and from local files. It understands headings,
 * paragraphs, lists, links, bold text, <pre>, <hr> and the common entities. It does NOT run
 * JavaScript, apply CSS or load images, and has no HTTPS (no TLS yet).
 *
 * usage: navegador [url]      keys: Up/Down/PgUp/PgDn scroll, Enter in the address bar to go. */

#include <gui.h>

#define W 820
#define H 580
#define BAR_H 36
#define STATUS_H 20
#define VIEW_H (H - BAR_H - STATUS_H)
#define MARGIN 16
#define PAGE_MAX (256 * 1024)
#define POOL_MAX (160 * 1024)
#define MAX_RUNS 8000
#define MAX_LINKS 200
#define HREF_MAX 160

#define C_BG    RGB(252, 252, 250)
#define C_TEXT  RGB(24, 28, 36)
#define C_LINK  RGB(30, 90, 200)
#define C_HEAD  RGB(20, 50, 110)
#define C_BAR   RGB(214, 218, 230)

enum { R_WORD, R_BREAK, R_BULLET, R_HR, R_PRE };

struct run {
    int off;                    /* text in pool */
    uint16_t len;
    uint8_t kind, scale;
    uint8_t bold, nosp;         /* nosp: glued to the previous word */
    uint16_t ind;               /* left indent in pixels (list items) */
    int16_t link;               /* index into hrefs, -1 none */
    uint32_t color;
};

static char page[PAGE_MAX + 1];
static long plen;
static char pool[POOL_MAX];
static int poolen;
static struct run runs[MAX_RUNS];
static int nruns;
static char hrefs[MAX_LINKS][HREF_MAX];
static int nlinks;
static int rx[MAX_RUNS], ry[MAX_RUNS];     /* layout result */
static int doc_h;
static int scroll = 0;

static char url[256];
static char hist[16][256];
static int nhist = 0;
static char title[64];
static char status_msg[96];
static int js_seen;
static int addr_focus = 0;
static char addr[256];
static int addr_len = 0;

/* ------------------------------------------------------------------ */
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int ncasecmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        int x = lower((unsigned char)a[i]), y = lower((unsigned char)b[i]);
        if (x != y) return x - y;
        if (!x) return 0;
    }
    return 0;
}

static const char *find_ci(const char *h, const char *needle, const char *end) {
    int n = (int)strlen(needle);
    for (; h + n <= end; h++) if (!ncasecmp(h, needle, n)) return h;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Fetching */
static int split_url(const char *u, char *host, int hs, int *port, const char **path) {
    if (strncmp(u, "http://", 7)) return -1;
    u += 7;
    int n = 0;
    while (u[n] && u[n] != '/' && u[n] != ':' && n < hs - 1) { host[n] = u[n]; n++; }
    host[n] = '\0';
    const char *r = u + n;
    *port = 80;
    if (*r == ':') { *port = atoi(r + 1); while (*r && *r != '/') r++; }
    *path = *r ? r : "/";
    return 0;
}

/* Build an absolute URL from a link found on the current page */
static void resolve(const char *base, const char *href, char *out, int cap) {
    if (!strncmp(href, "http://", 7) || !strncmp(href, "https://", 8) || !strncmp(href, "file://", 7)) {
        snprintf(out, (size_t)cap, "%s", href);
        return;
    }
    int is_http = !strncmp(base, "http://", 7);
    int origin_len = 0;
    if (is_http) {
        const char *p = base + 7;
        while (*p && *p != '/') p++;
        origin_len = (int)(p - base);
    }
    if (href[0] == '/') {
        snprintf(out, (size_t)cap, "%.*s%s", origin_len, is_http ? base : "", href);
        if (!is_http) snprintf(out, (size_t)cap, "%s", href);
        return;
    }
    /* relative: replace everything after the last '/' of the base */
    char dir[256];
    snprintf(dir, sizeof(dir), "%s", base);
    char *slash = strrchr(dir, '/');
    if (is_http && slash < dir + 7) {            /* "http://host" without a path */
        snprintf(out, (size_t)cap, "%s/%s", dir, href);
        return;
    }
    if (slash) slash[1] = '\0';
    snprintf(out, (size_t)cap, "%s%s", dir, href);
}

static int dechunk(char *buf, long len) {
    long in = 0, out = 0;
    while (in < len) {
        long sz = 0;
        while (in < len && buf[in] != '\r' && buf[in] != ';') {
            int c = lower((unsigned char)buf[in]);
            sz = sz * 16 + (c >= 'a' ? c - 'a' + 10 : c - '0');
            in++;
        }
        while (in < len && buf[in] != '\n') in++;
        in++;
        if (sz == 0) break;
        if (in + sz > len) sz = len - in;
        memmove(buf + out, buf + in, (size_t)sz);
        out += sz;
        in += sz + 2;
    }
    return (int)out;
}

/* Fills 'page'. Returns 0 on success; on failure sets status_msg and returns -1. */
static int fetch(char *u, int depth) {
    plen = 0;
    if (depth > 5) { snprintf(status_msg, sizeof(status_msg), "redirecionamentos demais"); return -1; }
    if (!strncmp(u, "https://", 8)) {
        snprintf(status_msg, sizeof(status_msg), "HTTPS ainda nao e suportado (sem TLS)");
        return -1;
    }
    if (strncmp(u, "http://", 7)) {                       /* local file */
        const char *path = !strncmp(u, "file://", 7) ? u + 7 : u;
        int fd = open(path, O_RDONLY);
        if (fd < 0) { snprintf(status_msg, sizeof(status_msg), "arquivo nao encontrado: %s", path); return -1; }
        long n;
        while (plen < PAGE_MAX && (n = read(fd, page + plen, 2048)) > 0) plen += n;
        close(fd);
        page[plen] = '\0';
        return 0;
    }

    char host[96];
    int port;
    const char *path;
    split_url(u, host, sizeof(host), &port, &path);
    uint32_t ip;
    if (dns_resolve(host, &ip) < 0) { snprintf(status_msg, sizeof(status_msg), "nao foi possivel resolver %s", host); return -1; }
    int s = tcp_connect(ip, port);
    if (s < 0) { snprintf(status_msg, sizeof(status_msg), "falha ao conectar em %s:%d", host, port); return -1; }
    char req[640];
    int rl = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Mozilla/5.0 (Loop-OS) navegador\r\nAccept: text/html,*/*\r\nConnection: close\r\n\r\n",
                      path, host);
    if (write(s, req, (size_t)rl) < 0) { close(s); snprintf(status_msg, sizeof(status_msg), "falha ao enviar"); return -1; }
    long n;
    while (plen < PAGE_MAX && (n = read(s, page + plen, 2048)) > 0) plen += n;
    close(s);
    page[plen] = '\0';

    char *body = NULL;
    for (long i = 0; i + 3 < plen; i++) {
        if (!memcmp(page + i, "\r\n\r\n", 4)) { body = page + i + 4; break; }
    }
    if (!body) { snprintf(status_msg, sizeof(status_msg), "resposta invalida"); return -1; }
    int code = atoi(page + 9);
    long hlen = body - page;

    if (code >= 300 && code < 400) {
        const char *loc = find_ci(page, "\r\nlocation:", body);
        if (loc) {
            loc += 11;
            while (*loc == ' ') loc++;
            char target[256];
            int k = 0;
            while (loc[k] && loc[k] != '\r' && k < (int)sizeof(target) - 1) { target[k] = loc[k]; k++; }
            target[k] = '\0';
            char abs[256];
            resolve(u, target, abs, sizeof(abs));
            snprintf(u, 256, "%s", abs);
            return fetch(u, depth + 1);
        }
    }
    int chunked = find_ci(page, "transfer-encoding: chunked", body) != NULL;
    long blen = plen - hlen;
    memmove(page, body, (size_t)blen);
    plen = blen;
    if (chunked) plen = dechunk(page, plen);
    page[plen] = '\0';
    if (code >= 400) snprintf(status_msg, sizeof(status_msg), "servidor respondeu %d", code);
    return 0;
}

/* ------------------------------------------------------------------ */
/* HTML -> runs */
static int add_run(int kind, const char *text, int len, int scale, int bold, uint32_t color, int link, int ind, int nosp) {
    if (nruns >= MAX_RUNS || poolen + len + 1 > POOL_MAX) return -1;
    struct run *r = &runs[nruns++];
    r->kind = (uint8_t)kind;
    r->off = poolen;
    r->len = (uint16_t)len;
    r->scale = (uint8_t)scale;
    r->bold = (uint8_t)bold;
    r->color = color;
    r->link = (int16_t)link;
    r->ind = (uint16_t)ind;
    r->nosp = (uint8_t)nosp;
    if (len) memcpy(pool + poolen, text, (size_t)len);
    poolen += len;
    pool[poolen++] = '\0';
    return 0;
}

static int utf8_to_latin1(const unsigned char *s, int *used) {
    if (s[0] < 0x80) { *used = 1; return s[0]; }
    if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        int cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *used = 2;
        return cp < 256 ? cp : '?';
    }
    if ((s[0] & 0xF0) == 0xE0) {
        int cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *used = 3;
        if (cp == 0x2018 || cp == 0x2019) return '\'';
        if (cp == 0x201C || cp == 0x201D) return '"';
        if (cp == 0x2013 || cp == 0x2014) return '-';
        if (cp == 0x2022) return '*';
        return '?';
    }
    if ((s[0] & 0xF8) == 0xF0) { *used = 4; return '?'; }
    *used = 1;
    return s[0];                                     /* already Latin-1 */
}

static const struct { const char *name; unsigned char ch; } entities[] = {
    {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", ' '}, {"copy", 0xA9},
    {"reg", 0xAE}, {"deg", 0xB0}, {"middot", 0xB7}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"ccedil", 0xE7},
    {"Ccedil", 0xC7}, {"aacute", 0xE1}, {"eacute", 0xE9}, {"iacute", 0xED}, {"oacute", 0xF3}, {"uacute", 0xFA},
    {"Aacute", 0xC1}, {"Eacute", 0xC9}, {"Iacute", 0xCD}, {"Oacute", 0xD3}, {"Uacute", 0xDA}, {"atilde", 0xE3},
    {"otilde", 0xF5}, {"Atilde", 0xC3}, {"Otilde", 0xD5}, {"acirc", 0xE2}, {"ecirc", 0xEA}, {"ocirc", 0xF4},
    {"agrave", 0xE0}, {"ntilde", 0xF1}, {"uuml", 0xFC}, {"ouml", 0xF6}, {"auml", 0xE4}, {"euml", 0xEB},
    {"ndash", '-'}, {"mdash", '-'}, {"hellip", '.'}, {"bull", '*'}, {"times", 'x'}, {"euro", 'E'},
};

/* Decode one entity at s (which points at '&'). Returns the Latin-1 char and advances *adv. */
static int entity(const char *s, int *adv) {
    int n = 1;
    while (s[n] && s[n] != ';' && n < 10) n++;
    if (s[n] != ';') { *adv = 1; return '&'; }
    if (s[1] == '#') {
        int v = 0;
        if (s[2] == 'x' || s[2] == 'X') {
            for (int i = 3; i < n; i++) { int c = lower((unsigned char)s[i]); v = v * 16 + (c >= 'a' ? c - 'a' + 10 : c - '0'); }
        } else {
            for (int i = 2; i < n; i++) v = v * 10 + (s[i] - '0');
        }
        *adv = n + 1;
        return v < 256 ? (v ? v : '?') : '?';
    }
    for (unsigned i = 0; i < sizeof(entities) / sizeof(entities[0]); i++) {
        if ((int)strlen(entities[i].name) == n - 1 && !strncmp(s + 1, entities[i].name, (size_t)(n - 1))) {
            *adv = n + 1;
            return entities[i].ch;
        }
    }
    *adv = 1;
    return '&';
}

static void parse_html(void) {
    nruns = poolen = nlinks = 0;
    title[0] = '\0';
    js_seen = 0;

    int pre_open = 0;
    int bold = 0, scale = 1, link = -1, pre = 0, in_title = 0, depth = 0, glue = 0, blank_pending = 0;
    uint32_t color = C_TEXT;
    const char *p = page, *end = page + plen;
    char word[96];

    /* is this a plain-text file? */
    int is_html = find_ci(page, "<", end) != NULL;
    if (!is_html) pre = 1;

    #define BREAK(n) do { if (nruns > 0 && runs[nruns - 1].kind == R_BREAK) { if ((n) > runs[nruns - 1].ind) runs[nruns - 1].ind = (n); } \
                          else if (nruns > 0) add_run(R_BREAK, "", 0, 1, 0, 0, -1, (n), 0); glue = 0; } while (0)

    while (p < end) {
        if (*p == '<') {
            if (!strncmp(p, "<!--", 4)) {
                const char *e = find_ci(p + 4, "-->", end);
                p = e ? e + 3 : end;
                continue;
            }
            const char *q = p + 1;
            int closing = 0;
            if (*q == '/') { closing = 1; q++; }
            char name[16];
            int nl = 0;
            while (q < end && ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9')) && nl < 15) name[nl++] = (char)lower((unsigned char)*q++);
            name[nl] = '\0';
            /* find the end of the tag, honouring quotes */
            const char *tag_start = q, *e = q;
            char quote = 0;
            while (e < end && (quote || *e != '>')) {
                if (quote && *e == quote) quote = 0;
                else if (!quote && (*e == '"' || *e == '\'')) quote = *e;
                e++;
            }
            const char *tag_end = e;
            p = e < end ? e + 1 : end;

            if (!closing && (!strcmp(name, "script") || !strcmp(name, "style"))) {
                if (!strcmp(name, "script")) js_seen = 1;
                const char *close = find_ci(p, !strcmp(name, "script") ? "</script" : "</style", end);
                p = close ? close : end;
                continue;
            }
            if (!strcmp(name, "title")) { in_title = !closing; continue; }
            if (!strcmp(name, "head") || !strcmp(name, "html") || !strcmp(name, "body")) continue;

            if (!strcmp(name, "br")) { BREAK(1); }
            else if (!strcmp(name, "p") || !strcmp(name, "div") || !strcmp(name, "section") || !strcmp(name, "article") ||
                     !strcmp(name, "header") || !strcmp(name, "footer") || !strcmp(name, "nav") || !strcmp(name, "main") ||
                     !strcmp(name, "blockquote") || !strcmp(name, "form") || !strcmp(name, "table") || !strcmp(name, "tr") ||
                     !strcmp(name, "canvas") || !strcmp(name, "figure") || !strcmp(name, "center")) {
                BREAK(!strcmp(name, "p") ? 2 : 1);
            }
            else if (nl == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
                BREAK(2);
                if (closing) { bold = 0; scale = 1; color = C_TEXT; }
                else { bold = 1; scale = name[1] <= '2' ? 2 : 1; color = C_HEAD; }
            }
            else if (!strcmp(name, "b") || !strcmp(name, "strong") || !strcmp(name, "th")) { bold = !closing; if (!strcmp(name, "th") && closing) glue = 0; }
            else if (!strcmp(name, "i") || !strcmp(name, "em")) { /* no italics in this font */ }
            else if (!strcmp(name, "ul") || !strcmp(name, "ol")) { BREAK(1); depth += closing ? -1 : 1; if (depth < 0) depth = 0; }
            else if (!strcmp(name, "li")) {
                if (!closing) { BREAK(1); add_run(R_BULLET, "", 0, 1, 0, C_TEXT, -1, depth > 0 ? depth * 20 : 20, 0); }
            }
            else if (!strcmp(name, "hr")) { BREAK(1); add_run(R_HR, "", 0, 1, 0, 0, -1, 0, 0); BREAK(1); }
            else if (!strcmp(name, "pre")) { BREAK(2); pre = !closing; pre_open = !closing; if (closing) BREAK(2); }
            else if (!strcmp(name, "td")) { if (!closing) add_run(R_WORD, " ", 1, 1, 0, C_TEXT, -1, 0, 0); }
            else if (!strcmp(name, "a")) {
                if (closing) { link = -1; color = C_TEXT; }
                else {
                    link = -1;
                    const char *h = find_ci(tag_start, "href=", tag_end);
                    if (h && nlinks < MAX_LINKS) {
                        h += 5;
                        char qc = (*h == '"' || *h == '\'') ? *h++ : 0;
                        int k = 0;
                        while (h < tag_end && k < HREF_MAX - 1 && (qc ? *h != qc : (*h != ' ' && *h != '\t'))) hrefs[nlinks][k++] = *h++;
                        hrefs[nlinks][k] = '\0';
                        link = nlinks++;
                        color = C_LINK;
                    }
                }
            }
            else if (!strcmp(name, "img") || !strcmp(name, "input") || !strcmp(name, "button") || !strcmp(name, "select") || !strcmp(name, "textarea")) {
                if (!closing) {
                    char label[64];
                    snprintf(label, sizeof(label), "[%s]", !strcmp(name, "img") ? "imagem" : name);
                    const char *a = find_ci(tag_start, !strcmp(name, "img") ? "alt=" : "value=", tag_end);
                    if (a) {
                        a += !strcmp(name, "img") ? 4 : 6;
                        char qc = (*a == '"' || *a == '\'') ? *a++ : 0;
                        int k = 1;
                        label[0] = '[';
                        while (a < tag_end && k < 60 && (qc ? *a != qc : *a != ' ')) label[k++] = *a++;
                        label[k++] = ']'; label[k] = '\0';
                    }
                    add_run(R_WORD, label, (int)strlen(label), 1, 0, RGB(120, 124, 140), link, 0, 0);
                    glue = 0;
                }
            }
            (void)blank_pending;
            continue;
        }

        /* text up to the next tag */
        if (in_title) {
            int tl = (int)strlen(title);
            while (p < end && *p != '<' && tl < (int)sizeof(title) - 1) { if (*p != '\n' && *p != '\r') title[tl++] = *p; p++; }
            title[tl] = '\0';
            while (p < end && *p != '<') p++;
            continue;
        }
        if (pre) {
            char line[200];
            int ln = 0;
            if (pre_open && *p == '\n') p++;
            pre_open = 0;
            while (p < end && *p != '<') {
                if (*p == '\n' || ln >= (int)sizeof(line) - 2) {
                    add_run(R_PRE, line, ln, scale, 0, color, link, 0, 0);
                    ln = 0;
                    if (*p == '\n') p++;
                    continue;
                }
                if (*p == '\r') { p++; continue; }
                if (*p == '\t') { for (int t = 0; t < 4 && ln < (int)sizeof(line) - 2; t++) line[ln++] = ' '; p++; continue; }
                if (*p == '&') { int adv; line[ln++] = (char)entity(p, &adv); p += adv; continue; }
                int used;
                line[ln++] = (char)utf8_to_latin1((const unsigned char *)p, &used);
                p += used;
            }
            if (ln) add_run(R_PRE, line, ln, scale, 0, color, link, 0, 0);
            continue;
        }
        int first = 1;
        while (p < end && *p != '<') {
            if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') { glue = 0; first = 0; p++; continue; }
            int wl = 0;
            while (p < end && *p != '<' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t' && wl < (int)sizeof(word) - 2) {
                if (*p == '&') { int adv; int c = entity(p, &adv); word[wl++] = (char)(c == ' ' ? ' ' : c); p += adv; }
                else { int used; word[wl++] = (char)utf8_to_latin1((const unsigned char *)p, &used); p += used; }
            }
            if (wl) {
                add_run(R_WORD, word, wl, scale, bold, color, link, depth > 0 ? depth * 20 : 0, glue && first);
                glue = 1;
            }
            first = 0;
        }
    }
    if (nruns == 0) add_run(R_WORD, "(pagina vazia)", 14, 1, 0, RGB(120, 124, 140), -1, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Layout: assigns x/y to every run */
static void layout(void) {
    int width = W - 2 * MARGIN - 10;
    int cx = 0, cy = 8, line_h = 16, at_start = 1;
    int margin_done_at = -1;
    for (int i = 0; i < nruns; i++) {
        struct run *r = &runs[i];
        rx[i] = ry[i] = 0;
        switch (r->kind) {
        case R_BREAK:
            if (!at_start) { cy += line_h; cx = 0; at_start = 1; line_h = 16; }
            if (r->ind >= 2 && margin_done_at != cy) { cy += 10; margin_done_at = cy; }
            break;
        case R_BULLET:
            if (!at_start) { cy += line_h; cx = 0; line_h = 16; }
            rx[i] = r->ind - 12; ry[i] = cy;
            cx = r->ind;
            at_start = 0;
            break;
        case R_HR:
            rx[i] = 0; ry[i] = cy + 4;
            cy += 12; cx = 0; at_start = 1; line_h = 16;
            break;
        case R_PRE:
            if (!at_start) { cy += line_h; cx = 0; }
            rx[i] = 0; ry[i] = cy;
            line_h = 16 * r->scale;
            cy += line_h; cx = 0; at_start = 1; line_h = 16;
            break;
        default: {
            int w = r->len * 8 * r->scale;
            int sp = (at_start || r->nosp) ? 0 : 8 * r->scale;
            if (at_start && cx < r->ind) cx = r->ind;
            if (cx + sp + w > width && !at_start) {
                cy += line_h; cx = r->ind; sp = 0; line_h = 16;
            }
            if (r->scale * 16 > line_h) line_h = r->scale * 16;
            rx[i] = cx + sp; ry[i] = cy;
            cx += sp + w;
            at_start = 0;
        }
        }
    }
    doc_h = cy + line_h + 20;
}

static int link_at(int mx, int my) {
    for (int i = 0; i < nruns; i++) {
        struct run *r = &runs[i];
        if (r->kind != R_WORD || r->link < 0) continue;
        int x = MARGIN + rx[i], y = BAR_H + ry[i] - scroll;
        if (gui_inside(mx, my, x, y, r->len * 8 * r->scale, 16 * r->scale)) return r->link;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
static void render(struct gwin *g) {
    gui_fill(g, 0, 0, W, H, C_BG);

    /* page */
    for (int i = 0; i < nruns; i++) {
        struct run *r = &runs[i];
        int x = MARGIN + rx[i], y = BAR_H + ry[i] - scroll;
        if (y > H || y + 40 < BAR_H) continue;
        if (r->kind == R_HR) { gui_fill(g, MARGIN, y, W - 2 * MARGIN, 2, RGB(190, 194, 206)); continue; }
        if (r->kind == R_BULLET) { gui_fill(g, x + 2, y + 6, 5, 5, C_TEXT); continue; }
        if (r->kind == R_BREAK) continue;
        const char *t = pool + r->off;
        gui_text(g, x, y, t, r->color, r->scale);
        if (r->bold) gui_text(g, x + 1, y, t, r->color, r->scale);
        if (r->link >= 0) gui_fill(g, x, y + 15 * r->scale, r->len * 8 * r->scale, 1, r->color);
    }

    /* scroll bar */
    if (doc_h > VIEW_H) {
        int th = VIEW_H * VIEW_H / doc_h;
        if (th < 20) th = 20;
        int ty = BAR_H + (VIEW_H - th) * scroll / (doc_h - VIEW_H);
        gui_fill(g, W - 8, BAR_H, 8, VIEW_H, RGB(228, 230, 238));
        gui_fill(g, W - 8, ty, 8, th, RGB(150, 156, 180));
    }

    /* toolbar */
    gui_fill(g, 0, 0, W, BAR_H, C_BAR);
    gui_button(g, 6, 6, 56, 24, "Voltar", 0);
    gui_fill(g, 70, 6, W - 70 - 70, 24, addr_focus ? RGB(255, 255, 255) : RGB(240, 242, 248));
    gui_rect(g, 70, 6, W - 70 - 70, 24, addr_focus ? RGB(60, 110, 220) : RGB(140, 146, 170));
    const char *shown = addr_focus ? addr : url;
    int maxc = (W - 70 - 70 - 12) / 8;
    int sl = (int)strlen(shown);
    gui_text(g, 76, 10, sl > maxc ? shown + (sl - maxc) : shown, RGB(20, 24, 36), 1);
    if (addr_focus) gui_fill(g, 76 + (sl > maxc ? maxc : sl) * 8, 24, 8, 2, RGB(60, 110, 220));
    gui_button(g, W - 64, 6, 58, 24, "Ir", 0);

    /* status bar */
    gui_fill(g, 0, H - STATUS_H, W, STATUS_H, C_BAR);
    char b[160];
    if (status_msg[0]) snprintf(b, sizeof(b), "%s", status_msg);
    else if (js_seen) snprintf(b, sizeof(b), "%s%s(JavaScript desta pagina nao e executado)", title, title[0] ? "  -  " : "");
    else snprintf(b, sizeof(b), "%s", title[0] ? title : "pronto");
    gui_text(g, 8, H - STATUS_H + 2, b, js_seen && !status_msg[0] ? RGB(170, 90, 30) : RGB(50, 56, 80), 1);
    gui_present(g);
}

static void show_error_page(void) {
    snprintf(page, PAGE_MAX, "<h2>Nao foi possivel abrir a pagina</h2><p>%s</p><p>Enderecos aceitos: http://servidor/caminho e arquivos locais como /usr/www/index.html</p>", status_msg);
    plen = (long)strlen(page);
}

static void navigate(struct gwin *g, const char *target, int push) {
    if (push && url[0] && nhist < 16) snprintf(hist[nhist++], 256, "%s", url);
    snprintf(url, sizeof(url), "%s", target);
    snprintf(status_msg, sizeof(status_msg), "carregando...");
    render(g);
    status_msg[0] = '\0';
    if (fetch(url, 0) < 0) show_error_page();
    else status_msg[0] = '\0';
    parse_html();
    layout();
    scroll = 0;
}

static void go_address(struct gwin *g) {
    char t[256];
    snprintf(t, sizeof(t), "%s", addr);
    addr_focus = 0;
    if (!addr_len) return;
    if (!strncmp(t, "http", 4) || t[0] == '/' || !strncmp(t, "file:", 5)) navigate(g, t, 1);
    else {
        char u[270];
        snprintf(u, sizeof(u), "http://%s", t);
        navigate(g, u, 1);
    }
}

static void focus_address(void) {
    addr_focus = 1;
    snprintf(addr, sizeof(addr), "%s", url);
    addr_len = (int)strlen(addr);
}

int main(int argc, char **argv) {
    struct gwin g;
    if (gui_open(&g, W, H, "Navegador") < 0) return 1;
    navigate(&g, argc > 1 ? argv[1] : "/usr/www/index.html", 0);
    render(&g);

    struct win_event ev;
    while (gui_wait(&g, &ev) > 0) {
        if (ev.type == WIN_EV_CLOSE) return 0;
        int max_scroll = doc_h > VIEW_H ? doc_h - VIEW_H : 0;
        if (ev.type == WIN_EV_KEY) {
            unsigned char c = (unsigned char)ev.a;
            if (addr_focus) {
                if (c == '\n' || c == '\r') go_address(&g);
                else if ((c == '\b' || c == 127) && addr_len > 0) addr[--addr_len] = '\0';
                else if (c == 27) addr_focus = 0;
                else if (c >= 32 && c != 127 && addr_len < 250) { addr[addr_len++] = (char)c; addr[addr_len] = '\0'; }
            } else {
                if (c == 0x12 || c == 's') scroll += 48;
                else if (c == 0x11 || c == 'w') scroll -= 48;
                else if (c == 0x16 || c == ' ') scroll += VIEW_H - 32;
                else if (c == 0x15) scroll -= VIEW_H - 32;
                else if (c == 'g') focus_address();
            }
        } else if (ev.type == WIN_EV_MOUSE && ev.c == 1) {
            if (gui_inside(ev.a, ev.b, 6, 6, 56, 24)) {
                if (nhist > 0) {
                    char t[256];
                    snprintf(t, sizeof(t), "%s", hist[--nhist]);
                    navigate(&g, t, 0);
                }
            } else if (gui_inside(ev.a, ev.b, 70, 6, W - 140, 24)) {
                focus_address();
            } else if (gui_inside(ev.a, ev.b, W - 64, 6, 58, 24)) {
                if (addr_focus && addr_len) go_address(&g);
                else {
                    char t[256];
                    snprintf(t, sizeof(t), "%s", url);
                    navigate(&g, t, 0);
                }
            } else if (ev.b >= BAR_H && ev.b < H - STATUS_H) {
                addr_focus = 0;
                int l = link_at(ev.a, ev.b);
                if (l >= 0) {
                    char target[256];
                    resolve(url, hrefs[l], target, sizeof(target));
                    navigate(&g, target, 1);
                }
            }
        }
        if (scroll > max_scroll) scroll = max_scroll;
        if (scroll < 0) scroll = 0;
        render(&g);
    }
    return 0;
}
