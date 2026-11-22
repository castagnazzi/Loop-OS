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
};

struct sys_taskinfo {
    int32_t pid;
    int32_t ppid;
    int32_t state;        /* 1 ready, 2 running, 3 blocked, 4 zombie */
    char name[32];
};

#endif
