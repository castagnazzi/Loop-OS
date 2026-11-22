/* Kernel printf Implementation */

#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* Freestanding: no <sys/types.h>, so define ssize_t locally */
typedef long ssize_t;

/* Serial output lives in kernel/arch/x86_64/serial.c */
#include <kernel/arch/x86_64/serial.h>

/* Formatting helper */
struct printf_state {
    char *buf;
    size_t size;
    size_t pos;
    int (*putc)(int, void *);
    void *data;
    int count;
};

/* Everything printed through kprintf is kept in a small log buffer so a
 * display sink (framebuffer console) registered later can replay it. */
#define KLOG_SIZE 16384
static char klog_buf[KLOG_SIZE];
static size_t klog_len = 0;
static void (*klog_sink)(char) = NULL;

void kprintf_set_sink(void (*sink)(char)) {
    klog_sink = sink;
    if (sink) {
        for (size_t i = 0; i < klog_len; i++) sink(klog_buf[i]);
    }
}

static int printf_putc_serial(int c, void *data) {
    (void)data;
    serial_write_char(c);
    if (klog_len < KLOG_SIZE) klog_buf[klog_len++] = (char)c;
    if (klog_sink) klog_sink((char)c);
    return c;
}

static int printf_putc_buf(int c, void *data) {
    struct printf_state *st = data;
    if (st->pos < st->size - 1) {
        st->buf[st->pos++] = c;
    }
    st->count++;
    return c;
}

static void printf_number(struct printf_state *st, uint64_t val, int base, int width, int pad_zero, int upper) {
    char buf[32];
    int i = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    
    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val) {
            buf[i++] = digits[val % base];
            val /= base;
        }
    }
    
    while (i < width) {
        st->putc(pad_zero ? '0' : ' ', st->data);
        width--;
    }
    
    while (i--) st->putc(buf[i], st->data);
}

static void printf_signed(struct printf_state *st, int64_t val, int width, int pad_zero) {
    if (val < 0) {
        st->putc('-', st->data);
        val = -val;
        width--;
    }
    printf_number(st, val, 10, width, pad_zero, 0);
}

static int kvprintf_core(const char *fmt, va_list ap, struct printf_state *st) {
    st->count = 0;
    
    while (*fmt) {
        if (*fmt != '%') {
            st->putc(*fmt++, st->data);
            continue;
        }
        fmt++;
        
        /* Flags */
        int pad_zero = 0;
        int left_align = 0;
        int plus_sign = 0;
        int space_sign = 0;
        int hash = 0;
        
        while (1) {
            switch (*fmt) {
                case '0': pad_zero = 1; fmt++; break;
                case '-': left_align = 1; fmt++; break;
                case '+': plus_sign = 1; fmt++; break;
                case ' ': space_sign = 1; fmt++; break;
                case '#': hash = 1; fmt++; break;
                default: goto width_done;
            }
        }
    width_done:
        
        /* Width */
        int width = 0;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt++ - '0');
            }
        }
        
        /* Precision (ignored for now) */
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') { va_arg(ap, int); fmt++; }
            else { while (*fmt >= '0' && *fmt <= '9') fmt++; }
        }
        
        /* Length modifier */
        int long_long = 0, long_int = 0, short_int = 0, intmax = 0, size_t_len = 0, ptrdiff = 0;
        if (*fmt == 'h' && fmt[1] == 'h') { short_int = 1; fmt += 2; }
        else if (*fmt == 'h') { short_int = 1; fmt++; }
        else if (*fmt == 'l' && fmt[1] == 'l') { long_long = 1; fmt += 2; }
        else if (*fmt == 'l') { long_int = 1; fmt++; }
        else if (*fmt == 'j') { intmax = 1; fmt++; }
        else if (*fmt == 'z') { size_t_len = 1; fmt++; }
        else if (*fmt == 't') { ptrdiff = 1; fmt++; }
        
        /* Specifier */
        switch (*fmt++) {
            case 'd': case 'i': {
                int64_t val;
                if (long_long) val = va_arg(ap, long long);
                else if (long_int) val = va_arg(ap, long);
                else if (short_int) val = (short)va_arg(ap, int);
                else if (intmax) val = va_arg(ap, intmax_t);
                else if (size_t_len) val = va_arg(ap, ssize_t);
                else if (ptrdiff) val = va_arg(ap, ptrdiff_t);
                else val = va_arg(ap, int);
                printf_signed(st, val, width, pad_zero);
                break;
            }
            case 'u': {
                uint64_t val;
                if (long_long) val = va_arg(ap, unsigned long long);
                else if (long_int) val = va_arg(ap, unsigned long);
                else if (short_int) val = (unsigned short)va_arg(ap, unsigned int);
                else if (intmax) val = va_arg(ap, uintmax_t);
                else if (size_t_len) val = va_arg(ap, size_t);
                else if (ptrdiff) val = va_arg(ap, uintptr_t);
                else val = va_arg(ap, unsigned int);
                printf_number(st, val, 10, width, pad_zero, 0);
                break;
            }
            case 'x': case 'X': {
                uint64_t val;
                if (long_long) val = va_arg(ap, unsigned long long);
                else if (long_int) val = va_arg(ap, unsigned long);
                else if (short_int) val = (unsigned short)va_arg(ap, unsigned int);
                else if (intmax) val = va_arg(ap, uintmax_t);
                else if (size_t_len) val = va_arg(ap, size_t);
                else if (ptrdiff) val = va_arg(ap, uintptr_t);
                else val = va_arg(ap, unsigned int);
                printf_number(st, val, 16, width, pad_zero, *fmt == 'X');
                break;
            }
            case 'p': {
                uintptr_t val = va_arg(ap, uintptr_t);
                st->putc('0', st->data);
                st->putc('x', st->data);
                printf_number(st, val, 16, 2 * sizeof(uintptr_t), 1, 0);
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                size_t len = strlen(s);
                if (!left_align) {
                    while (width > len) { st->putc(' ', st->data); width--; }
                }
                while (*s) st->putc(*s++, st->data);
                while (width > len) { st->putc(' ', st->data); width--; }
                break;
            }
            case 'c': {
                char c = va_arg(ap, int);
                st->putc(c, st->data);
                break;
            }
            case '%': {
                st->putc('%', st->data);
                break;
            }
            default: {
                st->putc('%', st->data);
                st->putc(fmt[-1], st->data);
                break;
            }
        }
    }
    
    if (st->buf && st->pos < st->size) st->buf[st->pos] = '\0';
    return st->count;
}

int kvprintf(const char *fmt, va_list ap) {
    struct printf_state st = { .putc = printf_putc_serial };
    return kvprintf_core(fmt, ap, &st);
}

int kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = kvprintf(fmt, ap);
    va_end(ap);
    return ret;
}

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    struct printf_state st = { .buf = buf, .size = size, .putc = printf_putc_buf, .data = &st };
    kvprintf_core(fmt, ap, &st);
    return st.count;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

/* Panic */
__attribute__((noreturn)) void kernel_panic(const char *fmt, ...) {
    serial_write_string("\n*** KERNEL PANIC ***\n");
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    serial_write_string("\n");
    for (;;) asm volatile("cli; hlt");
}