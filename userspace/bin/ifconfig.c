/* ifconfig: show the network configuration */

#include <ulib.h>

static void show(const char *label, uint32_t ip) {
    printf("  %-8s %u.%u.%u.%u\n", label, ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}

int main(void) {
    struct sys_netinfo n;
    netinfo(&n);
    if (!n.present) {
        puts("no network card found");
        return 1;
    }
    printf("eth0 (%s): link %s\n", n.nic, n.link ? "up" : "down");
    printf("  mac      %02x:%02x:%02x:%02x:%02x:%02x\n", n.mac[0], n.mac[1], n.mac[2], n.mac[3], n.mac[4], n.mac[5]);
    if (!n.configured) {
        puts("  not configured yet (waiting for DHCP)");
        return 0;
    }
    show("address", n.ip);
    show("netmask", n.mask);
    show("gateway", n.gw);
    show("dns", n.dns);
    return 0;
}
