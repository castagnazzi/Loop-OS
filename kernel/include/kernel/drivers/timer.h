/* Timer Driver (PIT + HPET + TSC) */

#ifndef _KERNEL_DRIVERS_TIMER_H
#define _KERNEL_DRIVERS_TIMER_H

#include <stdint.h>

/* Initialize timer at given frequency (Hz) */
void timer_init(uint32_t frequency);

/* Get current tick count */
uint64_t timer_ticks(void);

/* Get current time in microseconds */
uint64_t timer_micros(void);

/* Sleep for milliseconds */
void timer_sleep_ms(uint32_t ms);

/* Sleep for microseconds */
void timer_sleep_us(uint64_t us);

/* Timer callback type */
typedef void (*timer_callback_t)(void *arg);

/* Register periodic callback */
int timer_register_callback(uint32_t hz, timer_callback_t cb, void *arg);

/* Unregister callback */
void timer_unregister_callback(int id);

#endif