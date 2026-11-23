/* Network interface drivers (polling, one NIC at a time).
 *
 * nic_init() probes the drivers in turn and keeps the first card that comes up. */

#ifndef _KERNEL_NET_NIC_H
#define _KERNEL_NET_NIC_H

#include <stddef.h>
#include <stdint.h>

struct nic_driver {
    const char *name;
    int (*init)(uint8_t mac[6]);                 /* 0 and the MAC, or -1 if no such card */
    int (*send)(const void *frame, size_t len);  /* 0 on success */
    void (*poll)(void (*handler)(const uint8_t *frame, size_t len));
    int (*link_up)(void);
};

extern const struct nic_driver e1000_driver;     /* Intel PRO/1000 (QEMU default for -device e1000) */
extern const struct nic_driver rtl8139_driver;   /* Realtek RTL8139 */
extern const struct nic_driver rtl8169_driver;   /* Realtek RTL8168/8169 (most PC motherboards) */
extern const struct nic_driver virtio_net_driver;/* virtio-net (QEMU, KVM, cloud) */

/* Probe every driver; returns 0 with the MAC filled, or -1 */
int nic_init(uint8_t mac[6]);
const char *nic_name(void);

int nic_send(const void *frame, size_t len);
void nic_poll(void (*handler)(const uint8_t *frame, size_t len));
int nic_link_up(void);

#endif
