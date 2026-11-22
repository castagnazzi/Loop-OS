/* Timer Driver Implementation (PIT) */

#include <kernel/drivers/timer.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

/* PIT ports */
#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL1 0x41
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43

/* PIT frequency */
#define PIT_BASE_FREQ 1193182

/* Timer state */
static uint64_t timer_tick_count = 0;
static uint32_t timer_frequency = 1000;
static uint64_t timer_micros_per_tick = 1000;

/* Callback system */
#define MAX_TIMER_CALLBACKS 16
struct timer_callback {
    uint32_t hz;
    uint64_t next_tick;
    timer_callback_t func;
    void *arg;
    int active;
};
static struct timer_callback timer_callbacks[MAX_TIMER_CALLBACKS];
static int next_callback_id = 1;

/* I/O helpers */
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Initialize PIT */
void timer_init(uint32_t frequency) {
    if (frequency < 18 || frequency > 1000000) {
        frequency = 1000;
    }
    
    timer_frequency = frequency;
    timer_micros_per_tick = 1000000 / frequency;
    
    /* Calculate divisor */
    uint16_t divisor = PIT_BASE_FREQ / frequency;
    if (divisor == 0) divisor = 1;
    
    /* Configure PIT channel 0: mode 3 (square wave), binary, LSB/MSB */
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
    
    /* Clear callbacks */
    memset(timer_callbacks, 0, sizeof(timer_callbacks));
    next_callback_id = 1;
    
    kprintf("Timer: initialized at %u Hz (divisor=%u)\n", frequency, divisor);
}

/* Timer interrupt handler (called from IRQ0) */
void timer_handler(void *frame) {
    (void)frame;
    timer_tick_count++;
    
    /* Check callbacks */
    for (int i = 0; i < MAX_TIMER_CALLBACKS; i++) {
        if (timer_callbacks[i].active && timer_tick_count >= timer_callbacks[i].next_tick) {
            timer_callbacks[i].func(timer_callbacks[i].arg);
            timer_callbacks[i].next_tick += timer_frequency / timer_callbacks[i].hz;
        }
    }
    
    /* EOI */
    pic_send_eoi(0);
}

uint64_t timer_ticks(void) {
    return timer_tick_count;
}

uint64_t timer_micros(void) {
    return timer_tick_count * timer_micros_per_tick;
}

void timer_sleep_ms(uint32_t ms) {
    uint64_t start = timer_ticks();
    uint64_t target = start + (ms * timer_frequency + 999) / 1000;
    while (timer_ticks() < target) {
        asm volatile("hlt");
    }
}

void timer_sleep_us(uint64_t us) {
    uint64_t start = timer_micros();
    while (timer_micros() - start < us) {
        asm volatile("pause");
    }
}

int timer_register_callback(uint32_t hz, timer_callback_t cb, void *arg) {
    if (!cb || hz == 0 || hz > timer_frequency) return -1;
    
    for (int i = 0; i < MAX_TIMER_CALLBACKS; i++) {
        if (!timer_callbacks[i].active) {
            timer_callbacks[i].hz = hz;
            timer_callbacks[i].next_tick = timer_tick_count + timer_frequency / hz;
            timer_callbacks[i].func = cb;
            timer_callbacks[i].arg = arg;
            timer_callbacks[i].active = 1;
            return next_callback_id++;
        }
    }
    return -1;
}

void timer_unregister_callback(int id) {
    /* Simple implementation: find by id (index+1) */
    if (id > 0 && id <= MAX_TIMER_CALLBACKS) {
        timer_callbacks[id - 1].active = 0;
    }
}
