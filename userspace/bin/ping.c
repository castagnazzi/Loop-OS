/* ping: send ICMP echo requests (4 by default) */

#include <ulib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: ping <host or address> [count]");
        return 1;
    }
    int count = argc > 2 ? atoi(argv[2]) : 4;
    uint32_t ip;
    if (dns_resolve(argv[1], &ip) < 0) {
        printf("ping: cannot resolve %s\n", argv[1]);
        return 1;
    }
    printf("PING %s (%u.%u.%u.%u)\n", argv[1], ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);

    int ok = 0;
    for (int i = 0; i < count; i++) {
        int rtt = ping_ip(ip, 2000);
        if (rtt >= 0) {
            printf("reply from %u.%u.%u.%u: time=%d ms\n", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255, rtt);
            ok++;
        } else {
            puts("request timed out");
        }
        if (i + 1 < count) sleep_ms(800);
    }
    printf("%d of %d replies\n", ok, count);
    return ok ? 0 : 1;
}
