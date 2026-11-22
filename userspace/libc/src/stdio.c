#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>

static int stdout_fd = STDOUT_FILENO;
static int stderr_fd = STDERR_FILENO;

int putchar(int c) {
    char ch = (char)c;
    return write(stdout_fd, &ch, 1) == 1 ? c : EOF;
}

int puts(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    if (write(STDOUT_FILENO, s, len) != (ssize_t)len) return EOF;
    if (write(STDOUT_FILENO, "\n", 1) != 1) return EOF;
    return 0;
}

int putchar_unlocked(int c) {
    return putchar(c);
}

int fputc(int c, FILE *stream) {
    (void)stream;
    return putchar(c);
}

int fputs(const char *s, FILE *stream) {
    (void)stream;
    return puts(s);
}

int puts_unlocked(const char *s) {
    return puts(s);
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    (void)stream;
    char buf[1024];
    int len = vsnprintf(buf, sizeof(buf), format, ap);
    if (len > 0) write(STDOUT_FILENO, buf, len);
    return len;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stream, format, ap);
    va_end(ap);
    return ret;
}

int printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stdout, format, ap);
    va_end(ap);
    return ret;
}

int vprintf(const char *format, va_list ap) {
    return vfprintf(stdout, format, ap);
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, 0x7fffffff, format, ap);
    va_end(ap);
    return ret;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    if (size == 0) return 0;
    
    char *buf = str;
    size_t remaining = size;
    int total = 0;
    
    while (*format && remaining > 1) {
        if (*format != '%') {
            *buf++ = *format++;
            remaining--;
            total++;
            continue;
        }
        format++;
        
        int width = 0;
        int zero_pad = 0;
        if (*format == '0') { zero_pad = 1; format++; }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format++ - '0');
        }
        
        char spec = *format++;
        char num_buf[32];
        int num_len = 0;
        
        switch (spec) {
            case 'd': case 'i': {
                int val = va_arg(ap, int);
                if (val < 0) {
                    if (remaining > 1) { *buf++ = '-'; remaining--; total++; }
                    val = -val;
                }
                char *p = num_buf + 31;
                *p = '\0';
                do { *--p = '0' + val % 10; val /= 10; num_len++; } while (val);
                while (width > num_len && remaining > 1) { *buf++ = zero_pad ? '0' : ' '; remaining--; total++; width--; }
                while (num_len-- && remaining > 1) { *buf++ = *p++; remaining--; total++; }
                break;
            }
            case 'u': {
                unsigned int val = va_arg(ap, unsigned int);
                char *p = num_buf + 31;
                *p = '\0';
                do { *--p = '0' + val % 10; val /= 10; num_len++; } while (val);
                while (width > num_len && remaining > 1) { *buf++ = zero_pad ? '0' : ' '; remaining--; total++; width--; }
                while (num_len-- && remaining > 1) { *buf++ = *p++; remaining--; total++; }
                break;
            }
            case 'x': case 'X': {
                unsigned int val = va_arg(ap, unsigned int);
                char *p = num_buf + 31;
                *p = '\0';
                const char *digits = (spec == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                do { *--p = digits[val & 15]; val >>= 4; num_len++; } while (val);
                while (width > num_len && remaining > 1) { *buf++ = zero_pad ? '0' : ' '; remaining--; total++; width--; }
                while (num_len-- && remaining > 1) { *buf++ = *p++; remaining--; total++; }
                break;
            }
            case 'p': {
                uintptr_t val = va_arg(ap, uintptr_t);
                char *p = num_buf + 31;
                *p = '\0';
                do { *--p = "0123456789abcdef"[val & 15]; val >>= 4; num_len++; } while (val);
                if (remaining > 2) { *buf++ = '0'; *buf++ = 'x'; remaining -= 2; total += 2; }
                while (num_len-- && remaining > 1) { *buf++ = *p++; remaining--; total++; }
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s && remaining > 1) { *buf++ = *s++; remaining--; total++; }
                break;
            }
            case 'c': {
                int c = va_arg(ap, int);
                if (remaining > 1) { *buf++ = c; remaining--; total++; }
                break;
            }
            case '%': {
                if (remaining > 1) { *buf++ = '%'; remaining--; total++; }
                break;
            }
            default: {
                if (remaining > 1) { *buf++ = '%'; remaining--; total++; }
                if (remaining > 1) { *buf++ = spec; remaining--; total++; }
                break;
            }
        }
    }
    
    if (remaining > 0) *buf = '\0';
    return total;
}

int fgetc(FILE *stream) {
    (void)stream;
    char c;
    return read(STDIN_FILENO, &c, 1) == 1 ? (unsigned char)c : EOF;
}

char *fgets(char *s, int size, FILE *stream) {
    (void)stream;
    if (size <= 0) return NULL;
    int i = 0;
    while (i < size - 1) {
        char c;
        ssize_t r = read(STDIN_FILENO, &c, 1);
        if (r <= 0) break;
        s[i++] = c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return i > 0 ? s : NULL;
}

FILE *fopen(const char *pathname, const char *mode) {
    (void)mode;
    int flags = O_RDONLY;
    if (strchr(mode, 'w')) flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (strchr(mode, 'a')) flags = O_WRONLY | O_CREAT | O_APPEND;
    else if (strchr(mode, '+')) flags = O_RDWR | O_CREAT;
    int fd = open(pathname, flags, 0644);
    return fd >= 0 ? (FILE *)(intptr_t)fd : NULL;
}

int fclose(FILE *stream) {
    return close((int)(intptr_t)stream);
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return read((int)(intptr_t)stream, ptr, size * nmemb) / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return write((int)(intptr_t)stream, ptr, size * nmemb) / size;
}

int fflush(FILE *stream) {
    (void)stream;
    return 0;
}

FILE *stdin = (FILE *)(intptr_t)STDIN_FILENO;
FILE *stdout = (FILE *)(intptr_t)STDOUT_FILENO;
FILE *stderr = (FILE *)(intptr_t)STDERR_FILENO;

int remove(const char *pathname) {
    return unlink(pathname);
}

int rename(const char *oldpath, const char *newpath) {
    return syscall_ret(syscall3(SYS_RENAME, (long)oldpath, (long)newpath, 0));
}