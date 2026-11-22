/* CMOS real time clock */

#ifndef _KERNEL_DRIVERS_RTC_H
#define _KERNEL_DRIVERS_RTC_H

struct rtc_time {
    int year, month, day, hour, minute, second;
};

/* Read the wall clock (UTC unless the firmware/emulator keeps local time) */
void rtc_read(struct rtc_time *t);

#endif
