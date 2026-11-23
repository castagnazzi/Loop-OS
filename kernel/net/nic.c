/* Picks the network driver that finds a card and forwards calls to it */

#include <kernel/net/nic.h>

static const struct nic_driver *const drivers[] = {
    &e1000_driver, &rtl8169_driver, &rtl8139_driver, &virtio_net_driver,
};

static const struct nic_driver *active = NULL;

int nic_init(uint8_t mac[6]) {
    for (unsigned i = 0; i < sizeof(drivers) / sizeof(drivers[0]); i++) {
        if (drivers[i]->init(mac) == 0) {
            active = drivers[i];
            return 0;
        }
    }
    return -1;
}

const char *nic_name(void) { return active ? active->name : "none"; }
int nic_send(const void *frame, size_t len) { return active ? active->send(frame, len) : -1; }
void nic_poll(void (*handler)(const uint8_t *frame, size_t len)) { if (active) active->poll(handler); }
int nic_link_up(void) { return active ? active->link_up() : 0; }
