/* wget: fetch a page over plain HTTP.
 *
 * usage: wget http://host[:port]/path [file]
 * Saves the body to 'file', or prints it. HTTPS is not supported yet. */

#include <ulib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: wget http://host[:port]/path [file]");
        return 1;
    }
    const char *url = argv[1];
    if (!strncmp(url, "https://", 8)) {
        puts("wget: https is not supported yet (no TLS)");
        return 1;
    }
    if (!strncmp(url, "http://", 7)) url += 7;

    char host[96];
    int port = 80;
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
    int s = tcp_connect(ip, port);
    if (s < 0) {
        printf("wget: cannot connect to %s:%d (error %d)\n", host, port, -s);
        return 1;
    }

    char req[512];
    int rl = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: loop-os-wget\r\nConnection: close\r\n\r\n", path, host);
    if (write(s, req, (size_t)rl) < 0) {
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
    int hlen = 0, in_body = 0;
    long total = 0;
    for (;;) {
        long r = read(s, buf, sizeof(buf));
        if (r < 0) { printf("wget: read error %ld\n", -r); break; }
        if (r == 0) break;
        long start = 0;
        if (!in_body) {
            for (long i = 0; i < r; i++) {
                if (hlen < (int)sizeof(head) - 1) head[hlen++] = buf[i];
                if (hlen >= 4 && !memcmp(&head[hlen - 4], "\r\n\r\n", 4)) {
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
    close(s);
    if (argc > 2) {
        close(out);
        printf("saved %ld bytes to %s\n", total, argv[2]);
    }
    return 0;
}
