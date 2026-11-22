/* nslookup: resolve a host name to an IPv4 address */

#include <ulib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: nslookup <host>");
        return 1;
    }
    uint32_t ip;
    if (dns_resolve(argv[1], &ip) < 0) {
        printf("nslookup: %s: not found\n", argv[1]);
        return 1;
    }
    printf("%s has address %u.%u.%u.%u\n", argv[1], ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    return 0;
}
