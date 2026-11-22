/* CMOS real time clock: registers 0x00.. through ports 0x70/0x71 */

#include <kernel/drivers/rtc.h>
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t v) { asm volatile("outb %0, %1" : : "a"(v), "Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t r; asm volatile("inb %1, %0" : "=a"(r) : "Nd"(port)); return r; }

static uint8_t cmos(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static int updating(void) { return cmos(0x0A) & 0x80; }

static void raw_read(uint8_t r[6]) {
    r[0] = cmos(0x00); r[1] = cmos(0x02); r[2] = cmos(0x04);
    r[3] = cmos(0x07); r[4] = cmos(0x08); r[5] = cmos(0x09);
}

void rtc_read(struct rtc_time *t) {
    uint8_t a[6], b[6];
    /* Read twice until two reads agree, so an update cannot tear the value */
    for (int tries = 0; tries < 5; tries++) {
        while (updating()) {}
        raw_read(a);
        while (updating()) {}
        raw_read(b);
        int same = 1;
        for (int i = 0; i < 6; i++) if (a[i] != b[i]) same = 0;
        if (same) break;
    }
    uint8_t regb = cmos(0x0B);
    int pm = b[2] & 0x80;
    b[2] &= 0x7F;
    if (!(regb & 0x04)) {                         /* BCD */
        for (int i = 0; i < 6; i++) b[i] = (uint8_t)((b[i] & 0x0F) + (b[i] >> 4) * 10);
    }
    int hour = b[2];
    if (!(regb & 0x02) && pm) hour = (hour % 12) + 12;   /* 12-hour mode */
    t->second = b[0]; t->minute = b[1]; t->hour = hour;
    t->day = b[3]; t->month = b[4]; t->year = 2000 + b[5];
}
