/* Network stack: Ethernet, ARP, IPv4, ICMP, UDP (DHCP, DNS) and a small TCP client.
 *
 * Everything is polled: net_poll() reads the card and handles what arrived.
 * The desktop calls it every tick and blocking calls call it while waiting.
 * IPv4 addresses are host-order integers (a.b.c.d = a<<24 | b<<16 | c<<8 | d). */

#ifndef _KERNEL_NET_NET_H
#define _KERNEL_NET_NET_H

#include <stddef.h>
#include <stdint.h>

struct netinfo {
    uint8_t mac[6];
    uint32_t ip, mask, gw, dns;
    int present;          /* a network card was found */
    int link;             /* cable/link is up */
    int configured;       /* DHCP gave us an address */
};

void net_init(void);
void net_poll(void);
const struct netinfo *net_info(void);

/* Blocking helpers (call them from a task, not from an interrupt) */
int net_ping(uint32_t ip, int timeout_ms);         /* round trip in ms, or -1 */
int net_dns(const char *name, uint32_t *ip);       /* 0 and the address, or -1 */

struct tcpsock;
struct tcpsock *tcp_connect(uint32_t ip, uint16_t port, int timeout_ms);   /* NULL on failure */
long tcp_recv(struct tcpsock *s, void *buf, size_t n);    /* >0 bytes, 0 end of stream, <0 error */
long tcp_send(struct tcpsock *s, const void *buf, size_t n);
long tcp_avail(struct tcpsock *s);                        /* bytes waiting, -1 at end of stream */
void tcp_ref(struct tcpsock *s);
void tcp_unref(struct tcpsock *s);                        /* last reference closes the connection */

#endif
