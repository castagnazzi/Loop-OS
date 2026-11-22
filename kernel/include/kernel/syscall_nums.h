/* System call numbers and ABI structures, shared by the kernel and userspace.
 *
 * Convention (same registers as Linux x86_64): number in RAX, arguments in
 * RDI, RSI, RDX, R10, R8, R9; result in RAX. Errors are returned as
 * negative errno values (-1 .. -4095). RCX and R11 are clobbered by the
 * SYSCALL instruction itself. */

#ifndef _KERNEL_SYSCALL_NUMS_H
#define _KERNEL_SYSCALL_NUMS_H

#include <stdint.h>

#define SYS_READ        0    /* read(fd, buf, count) */
#define SYS_WRITE       1    /* write(fd, buf, count) */
#define SYS_OPEN        2    /* open(path, flags) */
#define SYS_CLOSE       3    /* close(fd) */
#define SYS_LSEEK       8    /* lseek(fd, offset, whence) */
#define SYS_PIPE        22   /* pipe(int fds[2]) */
#define SYS_DUP2        33   /* dup2(oldfd, newfd) */
#define SYS_FORK        57   /* fork() -> child pid, 0 in the child */
#define SYS_EXECVE      59   /* execve(path, argv) */
#define SYS_MKDIR       83   /* mkdir(path) */
#define SYS_UNLINK      87   /* unlink(path): files and empty directories */
#define SYS_YIELD       24   /* yield() */
#define SYS_GETPID      39   /* getpid() */
#define SYS_EXIT        60   /* exit(code) */
#define SYS_WAIT4       61   /* wait4(pid, &status, options, 0) */
#define SYS_KILL        62   /* kill(pid, sig) */
#define SYS_READDIR     78   /* readdir(fd, struct sys_dirent *) -> 1, 0 at the end */
#define SYS_GETCWD      79   /* getcwd(buf, size) */
#define SYS_CHDIR       80   /* chdir(path) */

/* Loop-OS specific */
#define SYS_SBRK        100  /* sbrk(increment) -> previous break */
#define SYS_SPAWN       101  /* spawn(path, argv) -> pid (replaces fork+exec) */
#define SYS_UPTIME_MS   102  /* uptime_ms() */
#define SYS_SLEEP_MS    103  /* sleep_ms(ms) */
#define SYS_CLEAR       104  /* clear() the console */
#define SYS_TASKINFO    105  /* taskinfo(index, struct sys_taskinfo *) */

#define SYS_WIN_CREATE  110  /* win_create(w, h, title) -> window id */
#define SYS_WIN_PRESENT 111  /* win_present(id, pixels) - copies w*h 0x00RRGGBB pixels */
#define SYS_WIN_EVENT   112  /* win_event(id, struct win_event *, block) -> 1, 0 (none), < 0 */
#define SYS_WIN_DESTROY 113  /* win_destroy(id) */
#define SYS_FD_AVAIL    114  /* fd_avail(fd): bytes readable without blocking, -1 at EOF */
#define SYS_SYSINFO     116  /* sysinfo(struct sys_sysinfo *) */
#define SYS_NETINFO     117  /* netinfo(struct sys_netinfo *) */
#define SYS_DNS         118  /* dns(name, uint32_t *ip) -> 0 or < 0 */
#define SYS_PING        119  /* ping(ip, timeout_ms) -> round trip ms or < 0 */
#define SYS_TCP_CONNECT 120  /* tcp_connect(ip, port) -> fd (read/write/close like a file) */
#define SYS_WALLTIME    115  /* walltime() -> hour*3600 + minute*60 + second */

/* errno values used by the kernel */
#define SYS_ENOENT   2
#define SYS_ENOEXEC  8
#define SYS_EBADF    9
#define SYS_ECHILD   10
#define SYS_EAGAIN   11
#define SYS_ENOMEM   12
#define SYS_EACCES   13
#define SYS_EFAULT   14
#define SYS_EMFILE   24
#define SYS_EINVAL   22
#define SYS_ENOSYS   38

/* open() flags */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x40
#define O_TRUNC  0x200
#define O_APPEND 0x400

struct sys_dirent {
    char name[64];
    uint32_t is_dir;
    uint32_t pad;
    uint64_t size;        /* files: size in bytes */
};

struct sys_sysinfo {
    char os[16];
    char version[16];
    char kernel[24];
    char cpu[64];
    char vendor[16];
    uint64_t mem_total_kb, mem_free_kb;
    uint32_t fb_w, fb_h;
    uint32_t ntasks, ndisks;
    uint64_t uptime_ms;
};

struct sys_netinfo {
    uint8_t mac[6];
    uint8_t present, link, configured, pad;
    uint32_t ip, mask, gw, dns;     /* host order: a.b.c.d = a<<24 | b<<16 | c<<8 | d */
};

struct sys_taskinfo {
    int32_t pid;
    int32_t ppid;
    int32_t state;        /* 1 ready, 2 running, 3 blocked, 4 zombie */
    char name[32];
};

#endif
