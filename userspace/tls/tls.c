/* TLS client glue: BearSSL over the Loop-OS TCP file descriptor. */

#include <tls.h>
#include "bearssl/inc/bearssl.h"
#include "anchors.inc"

static br_ssl_client_context cc;
static br_x509_minimal_context xc;
static br_sslio_context ioc;
static unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
static int sock = -1;

/* No OS entropy source: BearSSL falls back to the seed we inject ourselves (see seed_engine). */
br_prng_seeder br_prng_seeder_system(const char **name) {
    if (name) *name = NULL;
    return 0;
}
static char errmsg[64];

static int low_read(void *ctx, unsigned char *buf, size_t len) {
    (void)ctx;
    long r = read(sock, buf, len);
    return r > 0 ? (int)r : -1;
}

static int low_write(void *ctx, const unsigned char *buf, size_t len) {
    (void)ctx;
    long r = write(sock, buf, len);
    return r > 0 ? (int)r : -1;
}

static inline unsigned long long rdtsc(void) {
    unsigned lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)hi << 32) | lo;
}

/* RDRAND when the CPU has it, mixed with timers; hashed down to 32 bytes. */
static void seed_engine(void) {
    unsigned eax = 1, ebx, ecx = 0, edx;
    asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    int has_rdrand = (ecx >> 30) & 1;
    br_sha256_context h;
    br_sha256_init(&h);
    for (int i = 0; i < 16; i++) {
        unsigned long long v[4];
        v[0] = rdtsc();
        v[1] = uptime_ms();
        v[2] = (unsigned long long)unixtime();
        v[3] = (unsigned long long)(long)&v[0];
        br_sha256_update(&h, v, sizeof(v));
        if (has_rdrand) {
            unsigned long long r = 0;
            unsigned char ok = 0;
            for (int t = 0; t < 10 && !ok; t++)
                asm volatile("rdrand %0; setc %1" : "=r"(r), "=qm"(ok) : : "cc");
            br_sha256_update(&h, &r, sizeof(r));
        }
        for (volatile int spin = 0; spin < 2000; spin++) { }
    }
    unsigned char seed[32];
    br_sha256_out(&h, seed);
    br_ssl_engine_inject_entropy(&cc.eng, seed, sizeof(seed));
}

static void set_error(int code) {
    switch (code) {
    case BR_ERR_X509_EXPIRED:         snprintf(errmsg, sizeof(errmsg), "certificado expirado ou relogio errado"); break;
    case BR_ERR_X509_NOT_TRUSTED:     snprintf(errmsg, sizeof(errmsg), "certificado de origem nao confiavel"); break;
    case BR_ERR_X509_BAD_SERVER_NAME: snprintf(errmsg, sizeof(errmsg), "certificado nao e deste site"); break;
    case BR_ERR_IO:                   snprintf(errmsg, sizeof(errmsg), "conexao encerrada durante o TLS"); break;
    default:                          snprintf(errmsg, sizeof(errmsg), "erro TLS %d", code); break;
    }
}

const char *tls_error(void) { return errmsg; }

int tls_open(uint32_t ip, int port, const char *host) {
    errmsg[0] = '\0';
    sock = tcp_connect(ip, port);
    if (sock < 0) { snprintf(errmsg, sizeof(errmsg), "falha ao conectar"); return -1; }

    br_ssl_client_init_full(&cc, &xc, TAs, TAs_NUM);
    long now = unixtime();
    br_x509_minimal_set_time(&xc, (uint32_t)(now / 86400 + 719528), (uint32_t)(now % 86400));
    br_ssl_engine_set_buffer(&cc.eng, iobuf, sizeof(iobuf), 1);
    seed_engine();
    if (!br_ssl_client_reset(&cc, host, 0)) {
        set_error(br_ssl_engine_last_error(&cc.eng));
        close(sock);
        sock = -1;
        return -1;
    }
    br_sslio_init(&ioc, &cc.eng, low_read, NULL, low_write, NULL);
    /* Drive the handshake now so that certificate errors surface here */
    if (br_sslio_flush(&ioc) < 0) {
        set_error(br_ssl_engine_last_error(&cc.eng));
        close(sock);
        sock = -1;
        return -1;
    }
    return 0;
}

long tls_write(const void *buf, size_t n) {
    if (sock < 0) return -1;
    if (br_sslio_write_all(&ioc, buf, n) < 0) { set_error(br_ssl_engine_last_error(&cc.eng)); return -1; }
    if (br_sslio_flush(&ioc) < 0) { set_error(br_ssl_engine_last_error(&cc.eng)); return -1; }
    return (long)n;
}

long tls_read(void *buf, size_t n) {
    if (sock < 0) return -1;
    int r = br_sslio_read(&ioc, buf, n);
    if (r >= 0) return r;
    int e = br_ssl_engine_last_error(&cc.eng);
    snprintf(errmsg, sizeof(errmsg), "fim da conexao (codigo %d)", e);
    if (e == BR_ERR_OK || e == BR_ERR_IO) return 0;        /* peer closed the stream */
    set_error(e);
    return -1;
}

void tls_close(void) {
    if (sock < 0) return;
    br_sslio_close(&ioc);
    close(sock);
    sock = -1;
}
