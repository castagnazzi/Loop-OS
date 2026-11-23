/* ulib: the minimal C library for Loop-OS user programs.
 *
 * Freestanding (no hosted libc yet): system call wrappers, a few string and
 * memory functions, and a small printf. Programs define
 *   int main(int argc, char **argv);
 * and crt0 calls it with the arguments the kernel put on the stack. */

#ifndef ULIB_H
#define ULIB_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <kernel/syscall_nums.h>

typedef long ssize_t;
typedef int pid_t;

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define WNOHANG 1

/* System calls. Errors come back as -errno (check for < 0). */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int open(const char *path, int flags);
int close(int fd);
long lseek(int fd, long offset, int whence);
void yield(void);
pid_t getpid(void);
__attribute__((noreturn)) void exit(int code);
int pipe(int fds[2]);
int dup2(int oldfd, int newfd);
pid_t fork(void);
int execve(const char *path, char *const argv[]);
int mkdir(const char *path);
int unlink(const char *path);                              /* files and empty directories */
int spawn(const char *path, char *const argv[]);          /* returns the new pid */
pid_t waitpid(pid_t pid, int *status, int options);       /* 0 if WNOHANG and still running */
int kill(pid_t pid, int sig);
int readdir(int fd, struct sys_dirent *entry);            /* 1 = got one, 0 = end */
int getcwd(char *buf, size_t size);
int chdir(const char *path);
void *sbrk(long increment);                                /* (void *)-1 on failure */
unsigned long uptime_ms(void);
void sleep_ms(unsigned long ms);
void clear_screen(void);
int fd_avail(int fd);                                      /* bytes readable now, -1 at EOF */
long walltime(void);                                       /* seconds since midnight */
int sysinfo(struct sys_sysinfo *info);
int netinfo(struct sys_netinfo *info);
int dns_resolve(const char *name, uint32_t *ip);         /* 0 or < 0; dotted quads work too */
int ping_ip(uint32_t ip, int timeout_ms);                /* round trip in ms, < 0 on timeout */
int keymap(const char *name);                          /* NULL: current layout (0 us, 1 br); "us"/"br" sets */
int tcp_connect(uint32_t ip, int port);                  /* fd (read/write/close), or < 0 */
int taskinfo(int index, struct sys_taskinfo *info);

/* Strings and memory */
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
int atoi(const char *s);

/* Output */
int putchar(int c);
int puts(const char *s);                                   /* appends a newline */
int printf(const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);

#endif
