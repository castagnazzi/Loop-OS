/* wget: fetch a page over HTTP or HTTPS.
 *
 * usage: wget http(s)://host[:port]/path [file]
 * Saves the body to 'file', or prints it. HTTPS checks the certificate against the root list. */

#include <ulib.h>
#include <tls.h>

static int secure;
static int sock;

static long net_read(void *b, size_t n) { return secure ? tls_read(b, n) : read(sock, b, n); }
static long net_write(const void *b, size_t n) { return secure ? tls_write(b, n) : write(sock, b, n); }

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: wget http://host[:port]/path [file]");
        return 1;
    }
    const char *url = argv[1];
    int port = 80;
    if (!strncmp(url, "https://", 8)) { secure = 1; port = 443; url += 8; }
    else if (!strncmp(url, "http://", 7)) url += 7;

    char host[96];
    size_t n = 0;
    while (url[n] && url[n] != '/' && url[n] != ':' && n < sizeof(host) - 1) { host[n] = url[n]; n++; }
    host[n] = '\0';
    const char *rest = url + n;
    if (*rest == ':') { port = atoi(rest + 1); while (*rest && *rest != '/') rest++; }
    const char *path = *rest ? rest : "/";

    uint32_t ip;
    if (dns_resolve(host, &ip) < 0) {
        printf("wget: cannot resolve %s\n", host);
        return 1;
    }
    if (secure) {
        if (tls_open(ip, port, host) < 0) {
            printf("wget: TLS failed: %s\n", tls_error());
            return 1;
        }
    } else {
        sock = tcp_connect(ip, port);
        if (sock < 0) {
            printf("wget: cannot connect to %s:%d (error %d)\n", host, port, -sock);
            return 1;
        }
    }

    char req[512];
    int rl = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: loop-os-wget\r\nConnection: close\r\n\r\n", path, host);
    if (net_write(req, (size_t)rl) < 0) {
        puts("wget: send failed");
        return 1;
    }

    int out = STDOUT_FILENO;
    if (argc > 2) {
        out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC);
        if (out < 0) {
            printf("wget: cannot write %s\n", argv[2]);
            return 1;
        }
    }

    /* Skip the header (up to a blank line) and report the status line */
    char buf[2048];
    char head[256];
    int hlen = 0, in_body = 0, state = 0;
    long total = 0;
    for (;;) {
        long r = net_read(buf, sizeof(buf));
        if (r < 0) { printf("wget: read error %ld\n", -r); break; }
        if (r == 0) break;
        long start = 0;
        if (!in_body) {
            for (long i = 0; i < r; i++) {
                if (hlen < (int)sizeof(head) - 1) head[hlen++] = buf[i];
                /* track the blank line separately: real headers are longer than 'head' */
                char c = buf[i];
                if (c == '\r') state = (state == 2) ? 3 : 1;
                else if (c == '\n') state = (state == 1) ? 2 : (state == 3) ? 4 : 0;
                else state = 0;
                if (state == 4) {
                    in_body = 1;
                    start = i + 1;
                    break;
                }
            }
            if (!in_body) continue;
            head[hlen] = '\0';
            char *eol = strchr(head, '\r');
            if (eol) *eol = '\0';
            if (argc > 2) printf("%s\n", head);
        }
        if (r > start) {
            write(out, buf + start, (size_t)(r - start));
            total += r - start;
        }
    }
    if (secure && total == 0) printf("wget: no data (%s)\n", tls_error());
    if (secure) tls_close(); else close(sock);
    if (argc > 2) {
        close(out);
        printf("saved %ld bytes to %s\n", total, argv[2]);
    }
    return 0;
}
