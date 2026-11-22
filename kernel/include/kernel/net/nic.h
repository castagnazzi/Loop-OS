/* Network interface driver interface (polling, one NIC) */

#ifndef _KERNEL_NET_NIC_H
#define _KERNEL_NET_NIC_H

#include <stddef.h>
#include <stdint.h>

/* Look for a supported card and bring it up. Returns 0 and fills 'mac', or -1. */
int e1000_init(uint8_t mac[6]);

/* Queue one Ethernet frame for sending. 0 on success. */
int e1000_send(const void *frame, size_t len);

/* Hand every waiting frame to 'handler' */
void e1000_poll(void (*handler)(const uint8_t *frame, size_t len));

int e1000_link_up(void);

#endif
