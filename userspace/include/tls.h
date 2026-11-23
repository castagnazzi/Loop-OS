/* tls: a blocking TLS 1.2 client (BearSSL) over a Loop-OS TCP socket.
 * One connection at a time. The server certificate is checked against the Mozilla root
 * list (tls/anchors.inc) and must match the host name. */
#ifndef _TLS_H
#define _TLS_H
#include <ulib.h>

/* Connects to ip:port, handshakes with SNI 'host'. 0 on success, < 0 on failure
 * (tls_error() then describes why). */
int tls_open(uint32_t ip, int port, const char *host);
long tls_read(void *buf, size_t n);          /* bytes, 0 at end of stream, < 0 on error */
long tls_write(const void *buf, size_t n);   /* n on success, < 0 on error */
void tls_close(void);
const char *tls_error(void);
#endif
