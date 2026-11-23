/* Network stack (see net.h) */

#include <kernel/net/net.h>
#include <kernel/net/nic.h>
#include <kernel/pci.h>
#include <kernel/task.h>
#include <kernel/drivers/timer.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

#define ETH_ARP 0x0806
#define ETH_IP  0x0800
#define IP_ICMP 1
#define IP_TCP  6
#define IP_UDP  17

static struct netinfo ni;

/* ---------------------------------------------------------------- */
/* Byte helpers (network order is big endian) */
static uint16_t get16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t get32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static uint32_t csum_add(uint32_t sum, const uint8_t *d, size_t len) {
    for (size_t i = 0; i + 1 < len; i += 2) sum += (uint32_t)((d[i] << 8) | d[i + 1]);
    if (len & 1) sum += (uint32_t)(d[len - 1] << 8);
    return sum;
}
static uint16_t csum_fold(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static const uint8_t BCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/* ---------------------------------------------------------------- */
/* Waiting: spin on the card for a moment, then sleep a tick at a time. Time
 * only moves while interrupts are on, so the spin is bounded by count. */
static int wait_for(volatile int *flag, int timeout_ms) {
    uint64_t end = timer_ticks() + (uint64_t)timeout_ms / 10 + 1;
    for (;;) {
        for (int i = 0; i < 3000; i++) {
            net_poll();
            if (*flag) return 0;
        }
        if (timer_ticks() >= end) return -1;
        task_block(W_SLEEP, 1);
        net_poll();
        if (*flag) return 0;
    }
}

/* ---------------------------------------------------------------- */
/* Ethernet */
static int eth_send(const uint8_t *dst, uint16_t type, const uint8_t *payload, size_t len) {
    static uint8_t frame[1600];
    if (len > 1500) return -1;
    memcpy(frame, dst, 6);
    memcpy(frame + 6, ni.mac, 6);
    put16(frame + 12, type);
    memcpy(frame + 14, payload, len);
    return nic_send(frame, 14 + len);
}

/* ---------------------------------------------------------------- */
/* ARP */
struct arp_entry { uint32_t ip; uint8_t mac[6]; int valid; };
static struct arp_entry arp_cache[8];
static int arp_next = 0;
static volatile int arp_got = 0;

static void arp_store(uint32_t ip, const uint8_t *mac) {
    for (int i = 0; i < 8; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) { memcpy(arp_cache[i].mac, mac, 6); return; }
    }
    struct arp_entry *e = &arp_cache[arp_next];
    arp_next = (arp_next + 1) % 8;
    e->ip = ip; e->valid = 1;
    memcpy(e->mac, mac, 6);
}

static void arp_send(uint16_t op, const uint8_t *dst_mac, uint32_t target_ip) {
    uint8_t p[28];
    put16(p, 1);                 /* Ethernet */
    put16(p + 2, ETH_IP);
    p[4] = 6; p[5] = 4;
    put16(p + 6, op);
    memcpy(p + 8, ni.mac, 6);
    put32(p + 14, ni.ip);
    if (op == 1) memset(p + 18, 0, 6); else memcpy(p + 18, dst_mac, 6);
    put32(p + 24, target_ip);
    eth_send(op == 1 ? BCAST : dst_mac, ETH_ARP, p, 28);
}

static void arp_rx(const uint8_t *p, size_t len) {
    if (len < 28 || get16(p) != 1 || get16(p + 2) != ETH_IP) return;
    uint16_t op = get16(p + 6);
    uint32_t sip = get32(p + 14), tip = get32(p + 24);
    arp_store(sip, p + 8);
    arp_got = 1;
    if (op == 1 && ni.ip && tip == ni.ip) arp_send(2, p + 8, sip);
}

static int arp_resolve(uint32_t ip, uint8_t mac[6]) {
    for (int attempt = 0; attempt < 4; attempt++) {
        for (int i = 0; i < 8; i++) {
            if (arp_cache[i].valid && arp_cache[i].ip == ip) { memcpy(mac, arp_cache[i].mac, 6); return 0; }
        }
        arp_got = 0;
        arp_send(1, NULL, ip);
        wait_for(&arp_got, 300);
    }
    return -1;
}

/* ---------------------------------------------------------------- */
/* IPv4 */
static uint16_t ip_id = 1;

static int ip_send(uint32_t dst, uint8_t proto, const uint8_t *payload, size_t len) {
    static uint8_t pkt[1520];
    if (len > 1480) return -1;
    uint8_t mac[6];
    if (dst == 0xFFFFFFFFu) {
        memcpy(mac, BCAST, 6);
    } else {
        uint32_t hop = ((dst & ni.mask) == (ni.ip & ni.mask)) ? dst : ni.gw;
        if (!hop || arp_resolve(hop, mac) != 0) return -1;
    }
    pkt[0] = 0x45; pkt[1] = 0;
    put16(pkt + 2, (uint16_t)(20 + len));
    put16(pkt + 4, ip_id++);
    put16(pkt + 6, 0x4000);
    pkt[8] = 64; pkt[9] = proto;
    put16(pkt + 10, 0);
    put32(pkt + 12, ni.ip);
    put32(pkt + 16, dst);
    put16(pkt + 10, csum_fold(csum_add(0, pkt, 20)));
    memcpy(pkt + 20, payload, len);
    return eth_send(mac, ETH_IP, pkt, 20 + len);
}

/* ---------------------------------------------------------------- */
/* ICMP */
static volatile int ping_got = 0;
static uint16_t ping_seq = 0;
static uint32_t ping_from = 0;

static void icmp_rx(uint32_t src, const uint8_t *p, size_t len) {
    if (len < 8) return;
    if (p[0] == 8) {                                     /* echo request: answer it */
        uint8_t reply[1480];
        if (len > sizeof(reply)) return;
        memcpy(reply, p, len);
        reply[0] = 0;
        put16(reply + 2, 0);
        put16(reply + 2, csum_fold(csum_add(0, reply, len)));
        ip_send(src, IP_ICMP, reply, len);
    } else if (p[0] == 0 && get16(p + 4) == 0x4C50 && get16(p + 6) == ping_seq && src == ping_from) {
        ping_got = 1;
    }
}

int net_ping(uint32_t ip, int timeout_ms) {
    if (!ni.present || !ni.configured) return -1;
    uint8_t p[40];
    memset(p, 0, sizeof(p));
    p[0] = 8;
    put16(p + 4, 0x4C50);
    ping_seq++;
    put16(p + 6, ping_seq);
    for (int i = 8; i < 40; i++) p[i] = (uint8_t)('a' + i % 23);
    put16(p + 2, csum_fold(csum_add(0, p, 40)));

    ping_got = 0;
    ping_from = ip;
    uint64_t t0 = timer_micros();
    if (ip_send(ip, IP_ICMP, p, 40) != 0) return -1;
    if (wait_for(&ping_got, timeout_ms) != 0) return -1;
    uint64_t us = timer_micros() - t0;
    return (int)((us + 999) / 1000);
}

/* ---------------------------------------------------------------- */
/* UDP */
static int udp_send(uint32_t dst, uint16_t sport, uint16_t dport, const uint8_t *data, size_t len) {
    static uint8_t d[1480];
    if (len + 8 > sizeof(d)) return -1;
    put16(d, sport);
    put16(d + 2, dport);
    put16(d + 4, (uint16_t)(8 + len));
    put16(d + 6, 0);                                     /* checksum optional over IPv4 */
    memcpy(d + 8, data, len);
    return ip_send(dst, IP_UDP, d, 8 + len);
}

/* ---------------------------------------------------------------- */
/* DHCP client */
static int dhcp_state = 0;                               /* 0 idle, 1 discover sent, 2 request sent, 3 done */
static uint32_t dhcp_xid = 0x4C4F4F50;
static uint32_t dhcp_offer_ip, dhcp_server;
static uint64_t dhcp_last = 0;
static int dhcp_tries = 0;

static void dhcp_send(int type) {
    uint8_t p[300];
    memset(p, 0, sizeof(p));
    p[0] = 1; p[1] = 1; p[2] = 6;
    put32(p + 4, dhcp_xid);
    put16(p + 10, 0x8000);                               /* broadcast reply */
    memcpy(p + 28, ni.mac, 6);
    put32(p + 236, 0x63825363);                          /* magic cookie */
    int o = 240;
    p[o++] = 53; p[o++] = 1; p[o++] = (uint8_t)type;
    if (type == 3) {
        p[o++] = 50; p[o++] = 4; put32(p + o, dhcp_offer_ip); o += 4;
        p[o++] = 54; p[o++] = 4; put32(p + o, dhcp_server); o += 4;
    }
    p[o++] = 55; p[o++] = 3; p[o++] = 1; p[o++] = 3; p[o++] = 6;
    p[o++] = 255;
    udp_send(0xFFFFFFFFu, 68, 67, p, (size_t)o);
}

static void dhcp_rx(const uint8_t *p, size_t len) {
    if (len < 244 || p[0] != 2 || get32(p + 4) != dhcp_xid || get32(p + 236) != 0x63825363) return;
    int type = 0;
    uint32_t mask = 0, gw = 0, dns = 0, server = 0;
    for (size_t o = 240; o + 1 < len;) {
        uint8_t code = p[o++];
        if (code == 255) break;
        if (code == 0) continue;
        uint8_t l = p[o++];
        if (o + l > len) break;
        if (code == 53) type = p[o];
        else if (code == 1 && l >= 4) mask = get32(p + o);
        else if (code == 3 && l >= 4) gw = get32(p + o);
        else if (code == 6 && l >= 4) dns = get32(p + o);
        else if (code == 54 && l >= 4) server = get32(p + o);
        o += l;
    }
    uint32_t yiaddr = get32(p + 16);
    if (type == 2 && dhcp_state == 1) {                  /* OFFER */
        dhcp_offer_ip = yiaddr;
        dhcp_server = server;
        dhcp_state = 2;
        dhcp_send(3);
    } else if (type == 5 && dhcp_state == 2) {           /* ACK */
        ni.ip = yiaddr;
        ni.mask = mask ? mask : 0xFFFFFF00u;
        ni.gw = gw;
        ni.dns = dns ? dns : gw;
        ni.configured = 1;
        dhcp_state = 3;
        kprintf("net: DHCP ok, ip %u.%u.%u.%u gw %u.%u.%u.%u dns %u.%u.%u.%u\n",
                ni.ip >> 24, (ni.ip >> 16) & 255, (ni.ip >> 8) & 255, ni.ip & 255,
                ni.gw >> 24, (ni.gw >> 16) & 255, (ni.gw >> 8) & 255, ni.gw & 255,
                ni.dns >> 24, (ni.dns >> 16) & 255, (ni.dns >> 8) & 255, ni.dns & 255);
    }
}

static void dhcp_tick(void) {
    if (dhcp_state == 3 || dhcp_tries > 8) return;
    uint64_t now = timer_ticks();
    if (dhcp_state == 0 || now - dhcp_last > 200) {
        dhcp_last = now;
        dhcp_tries++;
        dhcp_state = 1;
        dhcp_send(1);
    }
}

/* ---------------------------------------------------------------- */
/* DNS */
static uint16_t dns_id = 0x1000;
static volatile int dns_got = 0;
static uint32_t dns_result = 0;

static void dns_rx(const uint8_t *p, size_t len) {
    if (len < 12 || get16(p) != dns_id || !(p[2] & 0x80)) return;
    if ((p[3] & 0x0F) != 0) { dns_result = 0; dns_got = 1; return; }
    int qd = get16(p + 4), an = get16(p + 6);
    size_t o = 12;
    for (int i = 0; i < qd; i++) {                       /* skip the question */
        while (o < len && p[o]) { if ((p[o] & 0xC0) == 0xC0) { o++; break; } o += p[o] + 1; }
        o += 1 + 4;
    }
    for (int i = 0; i < an && o < len; i++) {
        while (o < len && p[o]) { if ((p[o] & 0xC0) == 0xC0) { o++; break; } o += p[o] + 1; }
        o++;
        if (o + 10 > len) break;
        uint16_t type = get16(p + o), rdlen = get16(p + o + 8);
        o += 10;
        if (type == 1 && rdlen == 4 && o + 4 <= len) {
            dns_result = get32(p + o);
            dns_got = 1;
            return;
        }
        o += rdlen;
    }
    dns_result = 0;
    dns_got = 1;
}

static int parse_dotted(const char *s, uint32_t *ip) {
    uint32_t v = 0;
    int parts = 0;
    const char *p = s;
    while (*p) {
        if (*p < '0' || *p > '9') return -1;
        uint32_t n = 0;
        while (*p >= '0' && *p <= '9') { n = n * 10 + (uint32_t)(*p - '0'); p++; }
        if (n > 255) return -1;
        v = (v << 8) | n;
        parts++;
        if (*p == '.') p++;
        else if (*p) return -1;
    }
    if (parts != 4) return -1;
    *ip = v;
    return 0;
}

int net_dns(const char *name, uint32_t *ip) {
    if (parse_dotted(name, ip) == 0) return 0;
    if (!ni.present || !ni.configured || !ni.dns) return -1;

    uint8_t q[300];
    for (int attempt = 0; attempt < 3; attempt++) {
        size_t o = 12;
        memset(q, 0, 12);
        dns_id++;
        put16(q, dns_id);
        put16(q + 2, 0x0100);                            /* recursion desired */
        put16(q + 4, 1);
        const char *p = name;
        while (*p) {
            const char *dot = p;
            while (*dot && *dot != '.') dot++;
            size_t l = (size_t)(dot - p);
            if (l == 0 || l > 63 || o + l + 6 > sizeof(q)) return -1;
            q[o++] = (uint8_t)l;
            memcpy(q + o, p, l);
            o += l;
            p = *dot ? dot + 1 : dot;
        }
        q[o++] = 0;
        put16(q + o, 1); o += 2;                         /* type A */
        put16(q + o, 1); o += 2;                         /* class IN */

        dns_got = 0;
        dns_result = 0;
        if (udp_send(ni.dns, 53000, 53, q, o) != 0) continue;
        if (wait_for(&dns_got, 2000) == 0) {
            if (dns_result) { *ip = dns_result; return 0; }
            return -1;
        }
    }
    return -1;
}

static void udp_rx(uint32_t src, const uint8_t *p, size_t len) {
    (void)src;
    if (len < 8) return;
    uint16_t dport = get16(p + 2), ulen = get16(p + 4);
    if (ulen < 8 || ulen > len) return;
    if (dport == 68) dhcp_rx(p + 8, ulen - 8);
    else if (dport == 53000) dns_rx(p + 8, ulen - 8);
}

/* ---------------------------------------------------------------- */
/* TCP (client side, stop-and-wait) */
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10
#define TCP_RX_SIZE 32768
#define TCP_MAX_SOCKS 6
#define TCP_MSS 1400

enum { TS_CLOSED = 0, TS_SYN_SENT, TS_ESTABLISHED, TS_FIN_SENT };

struct tcpsock {
    int used, state, refs;
    uint16_t lport, rport;
    uint32_t rip;
    uint32_t snd_nxt, snd_una, rcv_nxt;
    uint8_t *rx;
    uint32_t rx_head, rx_count;
    volatile int fin, rst;
    uint32_t st_bytes, st_ooo, st_full, st_segs;     /* diagnostics */
};

static struct tcpsock socks[TCP_MAX_SOCKS];
static uint8_t sock_bufs[TCP_MAX_SOCKS][TCP_RX_SIZE];
static uint16_t next_port = 49200;

static int seq_gt(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }

static void tcp_seg(struct tcpsock *s, uint32_t seq, uint8_t flags, const uint8_t *data, size_t len) {
    uint8_t seg[1480];
    size_t hl = (flags & TCP_SYN) ? 24 : 20;
    if (hl + len > sizeof(seg)) return;
    put16(seg, s->lport);
    put16(seg + 2, s->rport);
    put32(seg + 4, seq);
    put32(seg + 8, (flags & TCP_ACK) ? s->rcv_nxt : 0);
    seg[12] = (uint8_t)((hl / 4) << 4);
    seg[13] = flags;
    put16(seg + 14, (uint16_t)(TCP_RX_SIZE - s->rx_count));    /* real free space: flow control */
    put16(seg + 16, 0);
    put16(seg + 18, 0);
    if (flags & TCP_SYN) { seg[20] = 2; seg[21] = 4; put16(seg + 22, TCP_MSS); }
    memcpy(seg + hl, data, len);

    uint8_t pseudo[12];
    put32(pseudo, ni.ip);
    put32(pseudo + 4, s->rip);
    pseudo[8] = 0; pseudo[9] = IP_TCP;
    put16(pseudo + 10, (uint16_t)(hl + len));
    uint32_t sum = csum_add(0, pseudo, 12);
    sum = csum_add(sum, seg, hl + len);
    put16(seg + 16, csum_fold(sum));
    ip_send(s->rip, IP_TCP, seg, hl + len);
}

static void tcp_rx(uint32_t src, const uint8_t *p, size_t len) {
    if (len < 20) return;
    uint16_t sport = get16(p), dport = get16(p + 2);
    uint32_t seq = get32(p + 4), ack = get32(p + 8);
    size_t hl = (size_t)(p[12] >> 4) * 4;
    uint8_t flags = p[13];
    if (hl < 20 || hl > len) return;

    struct tcpsock *s = NULL;
    for (int i = 0; i < TCP_MAX_SOCKS; i++) {
        if (socks[i].used && socks[i].state != TS_CLOSED && socks[i].rip == src &&
            socks[i].rport == sport && socks[i].lport == dport) { s = &socks[i]; break; }
    }
    if (!s) return;

    if (flags & TCP_RST) { s->rst = 1; s->state = TS_CLOSED; return; }

    if (s->state == TS_SYN_SENT) {
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK) && ack == s->snd_nxt) {
            s->rcv_nxt = seq + 1;
            s->snd_una = ack;
            s->state = TS_ESTABLISHED;
            tcp_seg(s, s->snd_nxt, TCP_ACK, NULL, 0);
        }
        return;
    }

    if ((flags & TCP_ACK) && seq_gt(ack, s->snd_una) && !seq_gt(ack, s->snd_nxt)) s->snd_una = ack;

    const uint8_t *data = p + hl;
    size_t dlen = len - hl;
    int need_ack = 0;
    if (dlen > 0) {
        need_ack = 1;
        s->st_segs++;
        if (seq == s->rcv_nxt) {
            size_t space = TCP_RX_SIZE - s->rx_count;
            size_t take = dlen < space ? dlen : space;
            for (size_t i = 0; i < take; i++) s->rx[(s->rx_head + s->rx_count + i) % TCP_RX_SIZE] = data[i];
            s->rx_count += (uint32_t)take;
            s->rcv_nxt += (uint32_t)take;
            s->st_bytes += (uint32_t)take;
            if (take < dlen) { s->st_full++; flags &= (uint8_t)~TCP_FIN; }
        } else {
            s->st_ooo++;
            flags &= (uint8_t)~TCP_FIN;                  /* out of order: duplicate ACK below */
        }
    }
    if ((flags & TCP_FIN) && seq + (uint32_t)dlen == s->rcv_nxt) {
        s->rcv_nxt++;
        s->fin = 1;
        need_ack = 1;
    }
    if (need_ack) tcp_seg(s, s->snd_nxt, TCP_ACK, NULL, 0);
}

struct tcpsock *tcp_connect(uint32_t ip, uint16_t port, int timeout_ms) {
    if (!ni.present || !ni.configured) return NULL;
    struct tcpsock *s = NULL;
    for (int i = 0; i < TCP_MAX_SOCKS; i++) {
        if (!socks[i].used) { s = &socks[i]; s->rx = sock_bufs[i]; break; }
    }
    if (!s) return NULL;
    uint8_t *rx = s->rx;
    memset(s, 0, sizeof(*s));
    s->rx = rx;
    s->used = 1;
    s->refs = 1;
    s->lport = next_port++;
    if (next_port > 60000) next_port = 49200;
    s->rport = port;
    s->rip = ip;
    uint32_t iss = (uint32_t)(timer_micros() * 2654435761u);
    s->snd_nxt = iss + 1;
    s->snd_una = iss;
    s->state = TS_SYN_SENT;

    int tries = timeout_ms / 1000 + 1;
    if (tries > 5) tries = 5;
    for (int t = 0; t < tries; t++) {
        tcp_seg(s, iss, TCP_SYN, NULL, 0);
        int slice = timeout_ms / tries;
        uint64_t end = timer_ticks() + (uint64_t)slice / 10 + 1;
        while (s->state == TS_SYN_SENT && !s->rst && timer_ticks() < end) {
            for (int i = 0; i < 3000 && s->state == TS_SYN_SENT; i++) net_poll();
            if (s->state == TS_SYN_SENT) task_block(W_SLEEP, 1);
        }
        if (s->state == TS_ESTABLISHED) return s;
        if (s->rst) break;
    }
    s->used = 0;
    s->state = TS_CLOSED;
    return NULL;
}

static void wait_acked(struct tcpsock *s, uint32_t target, int ms) {
    uint64_t end = timer_ticks() + (uint64_t)ms / 10 + 1;
    while (s->snd_una != target && !s->rst && timer_ticks() < end) {
        for (int i = 0; i < 3000 && s->snd_una != target; i++) net_poll();
        if (s->snd_una != target) task_block(W_SLEEP, 1);
    }
}

long tcp_send(struct tcpsock *s, const void *buf, size_t n) {
    const uint8_t *d = buf;
    size_t done = 0;
    while (done < n) {
        if (s->state != TS_ESTABLISHED) return done ? (long)done : -32;
        size_t chunk = n - done < TCP_MSS ? n - done : TCP_MSS;
        uint32_t seq = s->snd_nxt;
        s->snd_nxt += (uint32_t)chunk;
        int ok = 0;
        for (int attempt = 0; attempt < 6 && !ok; attempt++) {
            tcp_seg(s, seq, TCP_PSH | TCP_ACK, d + done, chunk);
            wait_acked(s, s->snd_nxt, 300);
            ok = s->snd_una == s->snd_nxt;
            if (s->rst) break;
        }
        if (!ok) return done ? (long)done : -110;
        done += chunk;
    }
    return (long)done;
}

long tcp_avail(struct tcpsock *s) {
    net_poll();
    if (s->rx_count) return (long)s->rx_count;
    return (s->fin || s->rst || s->state == TS_CLOSED) ? -1 : 0;
}

long tcp_recv(struct tcpsock *s, void *buf, size_t n) {
    uint64_t end = timer_ticks() + 1500;                 /* give up after 15 s of silence */
    while (s->rx_count == 0) {
        if (s->fin || s->rst || s->state == TS_CLOSED) return 0;
        for (int i = 0; i < 3000 && s->rx_count == 0 && !s->fin && !s->rst; i++) net_poll();
        if (s->rx_count == 0 && !s->fin && !s->rst) {
            if (timer_ticks() >= end) return -110;
            task_block(W_SLEEP, 1);
        }
    }
    uint8_t *o = buf;
    uint32_t free_before = TCP_RX_SIZE - s->rx_count;
    size_t k = n < s->rx_count ? n : s->rx_count;
    for (size_t i = 0; i < k; i++) o[i] = s->rx[(s->rx_head + i) % TCP_RX_SIZE];
    s->rx_head = (s->rx_head + (uint32_t)k) % TCP_RX_SIZE;
    s->rx_count -= (uint32_t)k;
    /* The sender may be waiting for room: tell it the window re-opened */
    if (free_before < 2 * TCP_MSS && TCP_RX_SIZE - s->rx_count >= 2 * TCP_MSS && s->state == TS_ESTABLISHED) {
        tcp_seg(s, s->snd_nxt, TCP_ACK, NULL, 0);
    }
    return (long)k;
}

void tcp_ref(struct tcpsock *s) { s->refs++; }

void tcp_unref(struct tcpsock *s) {
    if (--s->refs > 0) return;
    kprintf("tcp: close, %u segments, %u bytes, %u out of order, %u dropped (buffer full)\n",
            s->st_segs, s->st_bytes, s->st_ooo, s->st_full);
    if (s->state == TS_ESTABLISHED) {
        uint32_t seq = s->snd_nxt;
        s->snd_nxt++;
        s->state = TS_FIN_SENT;
        tcp_seg(s, seq, TCP_FIN | TCP_ACK, NULL, 0);
        wait_acked(s, s->snd_nxt, 300);
    }
    s->state = TS_CLOSED;
    s->used = 0;
}

/* ---------------------------------------------------------------- */
/* Receive path */
static void frame_rx(const uint8_t *f, size_t len) {
    if (len < 14) return;
    uint16_t type = get16(f + 12);
    const uint8_t *p = f + 14;
    size_t plen = len - 14;
    if (type == ETH_ARP) { arp_rx(p, plen); return; }
    if (type != ETH_IP || plen < 20 || (p[0] >> 4) != 4) return;

    size_t ihl = (size_t)(p[0] & 15) * 4;
    size_t total = get16(p + 2);
    if (ihl < 20 || total < ihl || total > plen) return;
    if (get16(p + 6) & 0x3FFF) return;                   /* fragments are not supported */
    uint32_t src = get32(p + 12), dst = get32(p + 16);
    if (ni.ip && dst != ni.ip && dst != 0xFFFFFFFFu && (dst & ~ni.mask) != ~ni.mask) return;

    const uint8_t *payload = p + ihl;
    size_t pl = total - ihl;
    switch (p[9]) {
    case IP_ICMP: icmp_rx(src, payload, pl); break;
    case IP_UDP:  udp_rx(src, payload, pl); break;
    case IP_TCP:  tcp_rx(src, payload, pl); break;
    default: break;
    }
}

void net_poll(void) {
    static int busy = 0;               /* handlers may wait (ARP): never poll inside a poll */
    if (!ni.present || busy) return;
    busy = 1;
    nic_poll(frame_rx);
    dhcp_tick();
    ni.link = nic_link_up();
    busy = 0;
}

const struct netinfo *net_info(void) { return &ni; }

void net_init(void) {
    memset(&ni, 0, sizeof(ni));
    pci_init();
    if (nic_init(ni.mac) != 0) {
        kprintf("net: no supported network card\n");
        return;
    }
    ni.present = 1;
    ni.link = nic_link_up();
    strncpy(ni.nic, nic_name(), sizeof(ni.nic) - 1);
    dhcp_tick();
}
