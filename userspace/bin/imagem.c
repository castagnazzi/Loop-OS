/* imagem: image viewer for PNG, BMP and LWP (the wallpaper format).
 *
 * usage: imagem file
 * Keys: f = fit / 100%, w a s d = pan at 100%, q = quit. */

#include <gui.h>

#define VW 640
#define VH 480
#define MAX_FILE (12 * 1024 * 1024)

static uint8_t *file;
static long flen;
static uint32_t *pix;           /* decoded 0x00RRGGBB */
static int iw, ih;

/* ------------------------------------------------------------------ */
/* inflate (RFC 1951), small and slow-ish but fine for pictures */
struct tree { uint16_t counts[16]; uint16_t symbols[288]; };
struct inf {
    const uint8_t *src; long srclen, pos;
    uint32_t bits; int nbits;
    uint8_t *dst; long dstcap, dpos;
    int err;
};

static int getbit(struct inf *d) {
    if (d->nbits == 0) {
        if (d->pos >= d->srclen) { d->err = 1; return 0; }
        d->bits = d->src[d->pos++];
        d->nbits = 8;
    }
    int b = d->bits & 1;
    d->bits >>= 1;
    d->nbits--;
    return b;
}

static uint32_t getbits(struct inf *d, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint32_t)getbit(d) << i;
    return v;
}

static void build_tree(struct tree *t, const uint8_t *lengths, int num) {
    uint16_t offs[16];
    for (int i = 0; i < 16; i++) t->counts[i] = 0;
    for (int i = 0; i < num; i++) t->counts[lengths[i]]++;
    t->counts[0] = 0;
    int sum = 0;
    for (int i = 0; i < 16; i++) { offs[i] = (uint16_t)sum; sum += t->counts[i]; }
    for (int i = 0; i < num; i++) if (lengths[i]) t->symbols[offs[lengths[i]]++] = (uint16_t)i;
}

static int decode_sym(struct inf *d, const struct tree *t) {
    int sum = 0, cur = 0, len = 0;
    do {
        cur = 2 * cur + getbit(d);
        len++;
        if (len > 15) { d->err = 1; return 0; }
        sum += t->counts[len];
        cur -= t->counts[len];
    } while (cur >= 0);
    return t->symbols[sum + cur];
}

static const uint16_t len_base[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const uint8_t len_extra[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const uint16_t dist_base[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const uint8_t dist_extra[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

static int inflate_block(struct inf *d, const struct tree *lt, const struct tree *dt) {
    for (;;) {
        int sym = decode_sym(d, lt);
        if (d->err) return -1;
        if (sym < 256) {
            if (d->dpos >= d->dstcap) return -1;
            d->dst[d->dpos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;
            int len = len_base[sym] + (int)getbits(d, len_extra[sym]);
            int ds = decode_sym(d, dt);
            if (d->err || ds >= 30) return -1;
            long dist = dist_base[ds] + (long)getbits(d, dist_extra[ds]);
            if (dist > d->dpos || d->dpos + len > d->dstcap) return -1;
            for (int i = 0; i < len; i++) { d->dst[d->dpos] = d->dst[d->dpos - dist]; d->dpos++; }
        }
    }
}

static int inflate_data(const uint8_t *src, long srclen, uint8_t *dst, long dstcap) {
    struct inf d = { src, srclen, 0, 0, 0, dst, dstcap, 0, 0 };
    static struct tree lt, dt;
    int last;
    do {
        last = getbit(&d);
        int type = (int)getbits(&d, 2);
        if (d.err) return -1;
        if (type == 0) {
            d.nbits = 0;
            if (d.pos + 4 > d.srclen) return -1;
            long n = d.src[d.pos] | (d.src[d.pos + 1] << 8);
            d.pos += 4;
            if (d.pos + n > d.srclen || d.dpos + n > d.dstcap) return -1;
            memcpy(d.dst + d.dpos, d.src + d.pos, (size_t)n);
            d.pos += n; d.dpos += n;
        } else if (type == 1) {
            uint8_t l[288];
            for (int i = 0; i < 144; i++) l[i] = 8;
            for (int i = 144; i < 256; i++) l[i] = 9;
            for (int i = 256; i < 280; i++) l[i] = 7;
            for (int i = 280; i < 288; i++) l[i] = 8;
            build_tree(&lt, l, 288);
            uint8_t dl[30];
            for (int i = 0; i < 30; i++) dl[i] = 5;
            build_tree(&dt, dl, 30);
            if (inflate_block(&d, &lt, &dt) != 0) return -1;
        } else if (type == 2) {
            int hlit = (int)getbits(&d, 5) + 257, hdist = (int)getbits(&d, 5) + 1, hclen = (int)getbits(&d, 4) + 4;
            static const uint8_t order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
            uint8_t lens[320];
            memset(lens, 0, sizeof(lens));
            for (int i = 0; i < hclen; i++) lens[order[i]] = (uint8_t)getbits(&d, 3);
            struct tree ct;
            build_tree(&ct, lens, 19);
            int n = 0;
            uint8_t all[320];
            while (n < hlit + hdist) {
                int sym = decode_sym(&d, &ct);
                if (d.err) return -1;
                if (sym < 16) { all[n++] = (uint8_t)sym; }
                else {
                    int prev = 0, rep;
                    if (sym == 16) { if (n == 0) return -1; prev = all[n - 1]; rep = 3 + (int)getbits(&d, 2); }
                    else if (sym == 17) rep = 3 + (int)getbits(&d, 3);
                    else rep = 11 + (int)getbits(&d, 7);
                    if (n + rep > hlit + hdist) return -1;
                    while (rep--) all[n++] = (uint8_t)prev;
                }
            }
            build_tree(&lt, all, hlit);
            build_tree(&dt, all + hlit, hdist);
            if (inflate_block(&d, &lt, &dt) != 0) return -1;
        } else {
            return -1;
        }
    } while (!last);
    return (int)d.dpos;
}

/* ------------------------------------------------------------------ */
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint32_t le32(const uint8_t *p) { return ((uint32_t)p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0]; }

static int decode_png(void) {
    if (flen < 33 || memcmp(file, "\x89PNG\r\n\x1a\n", 8)) return -1;
    uint32_t w = 0, h = 0;
    int depth = 0, ctype = 0, interlace = 0;
    uint8_t pal[256][3];
    int npal = 0;
    long zlen = 0;
    uint8_t *zbuf = sbrk((long)flen);                 /* concatenated IDAT data */
    if (zbuf == (void *)-1) return -1;
    long p = 8;
    while (p + 12 <= flen) {
        uint32_t len = be32(file + p);
        const uint8_t *type = file + p + 4, *data = file + p + 8;
        if (p + 12 + (long)len > flen) break;
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            w = be32(data); h = be32(data + 4);
            depth = data[8]; ctype = data[9]; interlace = data[12];
        } else if (!memcmp(type, "PLTE", 4)) {
            npal = (int)(len / 3) > 256 ? 256 : (int)(len / 3);
            for (int i = 0; i < npal; i++) { pal[i][0] = data[i * 3]; pal[i][1] = data[i * 3 + 1]; pal[i][2] = data[i * 3 + 2]; }
        } else if (!memcmp(type, "IDAT", 4)) {
            memcpy(zbuf + zlen, data, len);
            zlen += len;
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        p += 12 + len;
    }
    if (!w || !h || w > 8192 || h > 8192 || interlace || zlen < 6) return -1;
    if (depth != 8 && depth != 16) return -1;
    int ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!ch) return -1;
    int bpp = ch * depth / 8;
    long stride = (long)w * bpp;
    long rawsz = (stride + 1) * h;
    uint8_t *raw = sbrk(rawsz);
    if (raw == (void *)-1) return -1;
    if (inflate_data(zbuf + 2, zlen - 2, raw, rawsz) < 0) return -1;      /* skip the 2-byte zlib header */

    for (uint32_t y = 0; y < h; y++) {                                      /* undo the row filters */
        uint8_t *row = raw + (long)y * (stride + 1);
        uint8_t *cur = row + 1;
        const uint8_t *up = y ? row - (stride + 1) + 1 : NULL;
        int f = row[0];
        for (long x = 0; x < stride; x++) {
            int a = x >= bpp ? cur[x - bpp] : 0;
            int b = up ? up[x] : 0;
            int c = (up && x >= bpp) ? up[x - bpp] : 0;
            int v = cur[x];
            switch (f) {
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: {
                int pp = a + b - c, pa = pp > a ? pp - a : a - pp, pb = pp > b ? pp - b : b - pp, pc = pp > c ? pp - c : c - pp;
                v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                break;
            }
            }
            cur[x] = (uint8_t)v;
        }
    }

    pix = (uint32_t *)sbrk((long)w * h * 4);
    if (pix == (void *)-1) return -1;
    iw = (int)w; ih = (int)h;
    int step = depth / 8;                                                   /* 16-bit: keep the high byte */
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *s = raw + (long)y * (stride + 1) + 1;
        for (uint32_t x = 0; x < w; x++, s += bpp) {
            int r, g, b, a = 255;
            if (ctype == 0) { r = g = b = s[0]; }
            else if (ctype == 2) { r = s[0]; g = s[step]; b = s[2 * step]; }
            else if (ctype == 3) { int i = s[0] < npal ? s[0] : 0; r = pal[i][0]; g = pal[i][1]; b = pal[i][2]; }
            else if (ctype == 4) { r = g = b = s[0]; a = s[step]; }
            else { r = s[0]; g = s[step]; b = s[2 * step]; a = s[3 * step]; }
            if (a < 255) {                                                  /* blend over a checker backdrop */
                int bg = (((x >> 3) + (y >> 3)) & 1) ? 90 : 60;
                r = (r * a + bg * (255 - a)) / 255; g = (g * a + bg * (255 - a)) / 255; b = (b * a + bg * (255 - a)) / 255;
            }
            pix[y * w + x] = RGB(r, g, b);
        }
    }
    return 0;
}

static int decode_bmp(void) {
    if (flen < 54 || file[0] != 'B' || file[1] != 'M') return -1;
    uint32_t off = le32(file + 10), hdr = le32(file + 14);
    int32_t w = (int32_t)le32(file + 18), h = (int32_t)le32(file + 22);
    int bpp = file[28] | (file[29] << 8);
    uint32_t comp = le32(file + 30);
    if (hdr < 40 || comp != 0 || w <= 0 || w > 8192 || h == 0) return -1;
    int top_down = h < 0;
    if (top_down) h = -h;
    if (h > 8192 || (bpp != 24 && bpp != 32 && bpp != 8)) return -1;
    long stride = (((long)w * bpp + 31) / 32) * 4;
    if ((long)off + stride * h > flen) return -1;
    const uint8_t *palette = file + 14 + hdr;
    pix = (uint32_t *)sbrk((long)w * h * 4);
    if (pix == (void *)-1) return -1;
    iw = w; ih = h;
    for (int y = 0; y < h; y++) {
        const uint8_t *s = file + off + stride * (top_down ? y : h - 1 - y);
        for (int x = 0; x < w; x++) {
            int r, g, b;
            if (bpp == 8) { const uint8_t *c = palette + s[x] * 4; b = c[0]; g = c[1]; r = c[2]; }
            else { const uint8_t *c = s + x * (bpp / 8); b = c[0]; g = c[1]; r = c[2]; }
            pix[(long)y * w + x] = RGB(r, g, b);
        }
    }
    return 0;
}

static int decode_lwp(void) {
    if (flen < 12 || memcmp(file, "LWP1", 4)) return -1;
    uint32_t w = le32(file + 4), h = le32(file + 8);
    if (!w || !h || w > 8192 || h > 8192 || 12 + (long)w * h * 4 > flen) return -1;
    pix = (uint32_t *)sbrk((long)w * h * 4);
    if (pix == (void *)-1) return -1;
    iw = (int)w; ih = (int)h;
    for (long i = 0; i < (long)w * h; i++) {
        uint32_t v = le32(file + 12 + i * 4);
        int a = v >> 24, r = (v >> 16) & 255, g = (v >> 8) & 255, b = v & 255;
        pix[i] = RGB((r * a) / 255 + 20 * (255 - a) / 255, (g * a) / 255 + 24 * (255 - a) / 255, (b * a) / 255 + 40 * (255 - a) / 255);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
static int fit = 1, panx = 0, pany = 0;

static void render(struct gwin *g, const char *msg) {
    gui_fill(g, 0, 0, VW, VH, RGB(18, 20, 30));
    if (msg) {
        gui_text(g, 20, 20, msg, RGB(240, 140, 140), 1);
        gui_present(g);
        return;
    }
    int dw, dh, ox, oy;
    if (fit) {
        int sx = VW * 1000 / iw, sy = (VH - 24) * 1000 / ih;
        int sc = sx < sy ? sx : sy;
        if (sc > 1000) sc = 1000;                      /* never enlarge */
        dw = iw * sc / 1000; dh = ih * sc / 1000;
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        ox = (VW - dw) / 2; oy = (VH - 24 - dh) / 2;
        for (int y = 0; y < dh; y++) {
            const uint32_t *srow = pix + (long)(y * ih / dh) * iw;
            for (int x = 0; x < dw; x++) g->px[(long)(oy + y) * VW + ox + x] = srow[x * iw / dw];
        }
    } else {
        for (int y = 0; y < VH - 24; y++) {
            int sy = y + pany;
            if (sy < 0 || sy >= ih) continue;
            for (int x = 0; x < VW; x++) {
                int sx = x + panx;
                if (sx < 0 || sx >= iw) continue;
                g->px[(long)y * VW + x] = pix[(long)sy * iw + sx];
            }
        }
    }
    gui_fill(g, 0, VH - 24, VW, 24, RGB(30, 34, 52));
    char b[80];
    snprintf(b, sizeof(b), "%dx%d   %s   [f] ajustar/100%%  [wasd] mover", iw, ih, fit ? "ajustada" : "100%");
    gui_text(g, 8, VH - 20, b, RGB(190, 200, 220), 1);
    gui_present(g);
}

int main(int argc, char **argv) {
    struct gwin g;
    if (gui_open(&g, VW, VH, "Imagem") < 0) return 1;
    if (argc < 2) { render(&g, "uso: imagem arquivo.png|bmp|lwp"); goto wait; }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { render(&g, "nao foi possivel abrir o arquivo"); goto wait; }
    long size = lseek(fd, 0, 2);
    lseek(fd, 0, 0);
    if (size <= 0 || size > MAX_FILE) { render(&g, "arquivo vazio ou grande demais"); goto wait; }
    file = sbrk(size);
    if (file == (void *)-1) { render(&g, "sem memoria"); goto wait; }
    flen = 0;
    while (flen < size) {
        long r = read(fd, file + flen, size - flen > 2048 ? 2048 : size - flen);
        if (r <= 0) break;
        flen += r;
    }
    close(fd);

    const char *name = argv[1];
    int rc = -1;
    if (flen > 8 && !memcmp(file, "\x89PNG", 4)) rc = decode_png();
    else if (flen > 2 && file[0] == 'B' && file[1] == 'M') rc = decode_bmp();
    else if (flen > 4 && !memcmp(file, "LWP1", 4)) rc = decode_lwp();
    (void)name;
    if (rc != 0) { render(&g, "formato nao suportado (use PNG 8/16 bits, BMP ou LWP)"); goto wait; }
    render(&g, NULL);

wait:;
    struct win_event ev;
    while (gui_wait(&g, &ev) > 0) {
        if (ev.type == WIN_EV_CLOSE) return 0;
        if (ev.type == WIN_EV_KEY && pix) {
            if (ev.a == 'q') return 0;
            if (ev.a == 'f') { fit = !fit; panx = pany = 0; }
            else if (ev.a == 'a') panx -= 64;
            else if (ev.a == 'd') panx += 64;
            else if (ev.a == 'w') pany -= 64;
            else if (ev.a == 's') pany += 64;
            else continue;
            if (panx < 0) panx = 0;
            if (pany < 0) pany = 0;
            if (iw > VW && panx > iw - VW) panx = iw - VW;
            if (ih > VH - 24 && pany > ih - (VH - 24)) pany = ih - (VH - 24);
            if (iw <= VW) panx = 0;
            if (ih <= VH - 24) pany = 0;
            render(&g, NULL);
        }
    }
    return 0;
}
