/* ulib implementation: syscall wrappers, string functions, printf */

#include <ulib.h>

/* ------------------------------------------------------------------ */
/* Raw system calls */
static inline long sys0(long n) {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
    return ret;
}
static inline long sys1(long n, long a) {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(n), "D"(a) : "rcx", "r11", "memory");
    return ret;
}
static inline long sys2(long n, long a, long b) {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b) : "rcx", "r11", "memory");
    return ret;
}
static inline long sys3(long n, long a, long b, long c) {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return ret;
}
static inline long sys4(long n, long a, long b, long c, long d) {
    long ret;
    register long r10 asm("r10") = d;
    asm volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10)
                 : "rcx", "r11", "memory");
    return ret;
}

ssize_t read(int fd, void *buf, size_t count) { return sys3(SYS_READ, fd, (long)buf, (long)count); }
ssize_t write(int fd, const void *buf, size_t count) { return sys3(SYS_WRITE, fd, (long)buf, (long)count); }
int open(const char *path, int flags) { return (int)sys2(SYS_OPEN, (long)path, flags); }
int close(int fd) { return (int)sys1(SYS_CLOSE, fd); }
long lseek(int fd, long offset, int whence) { return sys3(SYS_LSEEK, fd, offset, whence); }
void yield(void) { sys0(SYS_YIELD); }
pid_t getpid(void) { return (pid_t)sys0(SYS_GETPID); }

void exit(int code) {
    sys1(SYS_EXIT, code);
    for (;;) asm volatile("ud2");
}

int spawn(const char *path, char *const argv[]) { return (int)sys2(SYS_SPAWN, (long)path, (long)argv); }
pid_t waitpid(pid_t pid, int *status, int options) {
    return (pid_t)sys4(SYS_WAIT4, pid, (long)status, options, 0);
}
int kill(pid_t pid, int sig) { return (int)sys2(SYS_KILL, pid, sig); }
int readdir(int fd, struct sys_dirent *entry) { return (int)sys2(SYS_READDIR, fd, (long)entry); }
int getcwd(char *buf, size_t size) { return (int)sys2(SYS_GETCWD, (long)buf, (long)size); }
int chdir(const char *path) { return (int)sys1(SYS_CHDIR, (long)path); }

void *sbrk(long increment) {
    long r = sys1(SYS_SBRK, increment);
    return r < 0 && r > -4096 ? (void *)-1 : (void *)r;
}

unsigned long uptime_ms(void) { return (unsigned long)sys0(SYS_UPTIME_MS); }
void sleep_ms(unsigned long ms) { sys1(SYS_SLEEP_MS, (long)ms); }
void clear_screen(void) { sys0(SYS_CLEAR); }
int taskinfo(int index, struct sys_taskinfo *info) { return (int)sys2(SYS_TASKINFO, index, (long)info); }

/* ------------------------------------------------------------------ */
/* Entry from crt0: the initial stack holds argc, argv[], NULL, envp[] */
extern int main(int argc, char **argv);

void _start_c(long *sp) {
    int argc = (int)sp[0];
    char **argv = (char **)&sp[1];
    exit(main(argc, argv));
}

/* ------------------------------------------------------------------ */
/* Strings and memory.
 * (Compiled with -fno-tree-loop-distribute-patterns so GCC does not turn
 * these loops back into calls to themselves.) */
size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) {}
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

char *strchr(const char *s, int c) {
    for (; *s; s++) if (*s == (char)c) return (char *)s;
    return c == 0 ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    for (; *s; s++) if (*s == (char)c) last = s;
    return c == 0 ? (char *)s : (char *)last;
}

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    uint8_t *d = dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (; n; n--, x++, y++) {
        if (*x != *y) return *x - *y;
    }
    return 0;
}

int atoi(const char *s) {
    int sign = 1, v = 0;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return sign * v;
}

/* ------------------------------------------------------------------ */
/* printf */
struct out {
    char *buf;
    size_t size;     /* capacity (0 = count only) */
    size_t len;      /* characters produced so far */
};

static void emit(struct out *o, char c) {
    if (o->size && o->len + 1 < o->size) o->buf[o->len] = c;
    o->len++;
}

static void emit_padded(struct out *o, const char *s, size_t n, int width, int left, char pad) {
    int fill = width > (int)n ? width - (int)n : 0;
    if (!left) while (fill--) emit(o, pad);
    for (size_t i = 0; i < n; i++) emit(o, s[i]);
    if (left) while (fill--) emit(o, ' ');
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    struct out o = { buf, size, 0 };

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            emit(&o, *fmt);
            continue;
        }
        fmt++;

        int left = 0, zero = 0, width = 0, longs = 0;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') { longs++; fmt++; }

        char tmp[32];
        switch (*fmt) {
        case 'd':
        case 'i': {
            long long v = longs ? va_arg(ap, long) : va_arg(ap, int);
            unsigned long long u = v < 0 ? -(unsigned long long)v : (unsigned long long)v;
            size_t n = 0;
            do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
            if (v < 0) tmp[n++] = '-';
            char rev[32];
            for (size_t i = 0; i < n; i++) rev[i] = tmp[n - 1 - i];
            emit_padded(&o, rev, n, width, left, zero && !left ? '0' : ' ');
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'p': {
            unsigned long long u;
            if (*fmt == 'p') { u = (unsigned long)va_arg(ap, void *); longs = 1; emit(&o, '0'); emit(&o, 'x'); }
            else u = longs ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            unsigned base = (*fmt == 'u') ? 10 : 16;
            const char *digits = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            size_t n = 0;
            do { tmp[n++] = digits[u % base]; u /= base; } while (u);
            char rev[32];
            for (size_t i = 0; i < n; i++) rev[i] = tmp[n - 1 - i];
            emit_padded(&o, rev, n, width, left, zero && !left ? '0' : ' ');
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            emit_padded(&o, s, strlen(s), width, left, ' ');
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            emit_padded(&o, &c, 1, width, left, ' ');
            break;
        }
        case '%':
            emit(&o, '%');
            break;
        default:
            emit(&o, '%');
            if (*fmt) emit(&o, *fmt); else fmt--;
            break;
        }
    }

    if (size) buf[o.len < size ? o.len : size - 1] = '\0';
    return (int)o.len;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    size_t len = n < (int)sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
    write(STDOUT_FILENO, buf, len);
    return n;
}

int putchar(int c) {
    char ch = (char)c;
    write(STDOUT_FILENO, &ch, 1);
    return c;
}

int puts(const char *s) {
    write(STDOUT_FILENO, s, strlen(s));
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}
