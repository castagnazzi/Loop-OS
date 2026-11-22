#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <syscall.h>
#include <errno.h>

/* Error handling */
extern int errno;

static inline long syscall_ret(long ret) {
    if (ret >= -128 && ret <= -1) {
        errno = -ret;
        return -1;
    }
    return ret;
}

/* File operations */
ssize_t read(int fd, void *buf, size_t count) {
    return syscall_ret(syscall3(SYS_READ, fd, (long)buf, count));
}

ssize_t write(int fd, const void *buf, size_t count) {
    return syscall_ret(syscall3(SYS_WRITE, fd, (long)buf, count));
}

int open(const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    return syscall_ret(syscall3(SYS_OPEN, (long)pathname, flags, mode));
}

int close(int fd) {
    return syscall_ret(syscall1(SYS_CLOSE, fd));
}

off_t lseek(int fd, off_t offset, int whence) {
    return syscall_ret(syscall3(SYS_LSEEK, fd, offset, whence));
}

int stat(const char *pathname, struct stat *statbuf) {
    return syscall_ret(syscall2(SYS_STAT, (long)pathname, (long)statbuf));
}

int fstat(int fd, struct stat *statbuf) {
    return syscall_ret(syscall2(SYS_FSTAT, fd, (long)statbuf));
}

int mkdir(const char *pathname, mode_t mode) {
    return syscall_ret(syscall2(SYS_MKDIR, (long)pathname, mode));
}

int rmdir(const char *pathname) {
    return syscall_ret(syscall1(SYS_RMDIR, (long)pathname));
}

int unlink(const char *pathname) {
    return syscall_ret(syscall1(SYS_UNLINK, (long)pathname));
}

int chdir(const char *path) {
    return syscall_ret(syscall1(SYS_CHDIR, (long)path));
}

char *getcwd(char *buf, size_t size) {
    long ret = syscall2(SYS_GETCWD, (long)buf, size);
    if (ret < 0) {
        errno = -ret;
        return NULL;
    }
    return buf;
}

/* Process */
pid_t fork(void) {
    return syscall_ret(syscall0(SYS_FORK));
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    return syscall_ret(syscall3(SYS_EXECVE, (long)path, (long)argv, (long)envp));
}

void _exit(int status) {
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}

pid_t getpid(void) {
    return syscall_ret(syscall0(SYS_GETPID));
}

pid_t getppid(void) {
    return syscall_ret(syscall0(SYS_GETPPID));
}

int wait4(pid_t pid, int *wstatus, int options, void *rusage) {
    return syscall_ret(syscall4(SYS_WAIT4, pid, (long)wstatus, options, (long)rusage));
}

/* Time */
int clock_gettime(int clock_id, struct timespec *ts) {
    return syscall_ret(syscall2(SYS_CLOCK_GETTIME, clock_id, (long)ts));
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    return syscall_ret(syscall2(SYS_NANOSLEEP, (long)req, (long)rem));
}

/* Memory */
void *sbrk(intptr_t increment) {
    long ret = syscall1(SYS_SBRK, increment);
    if (ret < 0) {
        errno = -ret;
        return (void *)-1;
    }
    return (void *)ret;
}

/* Directory */
int getdents(int fd, struct dirent *dirp, size_t count) {
    return syscall_ret(syscall3(SYS_GETDENTS, fd, (long)dirp, count));
}

/* Access */
int access(const char *pathname, int mode) {
    return syscall_ret(syscall2(SYS_ACCESS, (long)pathname, mode));
}

/* Fcntl */
int fcntl(int fd, int cmd, ...) {
    long arg = 0;
    if (cmd == F_SETFD || cmd == F_SETFL || cmd == F_SETLK || cmd == F_SETLKW) {
        va_list ap;
        va_start(ap, cmd);
        arg = va_arg(ap, long);
        va_end(ap);
    }
    return syscall_ret(syscall3(SYS_FCNTL, fd, cmd, arg));
}

/* Ioctl */
int ioctl(int fd, unsigned long request, ...) {
    long arg = 0;
    va_list ap;
    va_start(ap, request);
    arg = va_arg(ap, long);
    va_end(ap);
    return syscall_ret(syscall3(SYS_IOCTL, fd, request, arg));
}