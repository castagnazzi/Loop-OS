#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <sys/types.h>
#include <stdarg.h>

/* Syscall numbers (must match kernel) */
#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_OPEN        2
#define SYS_CLOSE       3
#define SYS_LSEEK       4
#define SYS_STAT        5
#define SYS_FSTAT       6
#define SYS_MKDIR       7
#define SYS_RMDIR       8
#define SYS_UNLINK      9
#define SYS_CHDIR       10
#define SYS_GETCWD      11
#define SYS_FORK        12
#define SYS_EXECVE      13
#define SYS_EXIT        14
#define SYS_WAIT4       15
#define SYS_GETPID      16
#define SYS_GETPPID     17
#define SYS_READV       18
#define SYS_WRITEV      19
#define SYS_IOCTL       20
#define SYS_FCNTL       21
#define SYS_GETDENTS    22
#define SYS_ACCESS      23
#define SYS_CLOCK_GETTIME 23
#define SYS_NANOSLEEP   24
#define SYS_SBRK        24
#define SYS_MMAP        25
#define SYS_MUNMAP      26

/* Inline syscall wrappers */
static inline long syscall0(long n) {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall1(long n, long a1) {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall2(long n, long a1, long a2) {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall3(long n, long a1, long a2, long a3) {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall4(long n, long a1, long a2, long a3, long a4) {
    long ret;
    register long r10 asm("r10") = a4;
    asm volatile("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall5(long n, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    register long r10 asm("r10") = a4;
    register long r8  asm("r8")  = a5;
    asm volatile("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    register long r10 asm("r10") = a4;
    register long r8  asm("r8")  = a5;
    register long r9  asm("r9")  = a6;
    asm volatile("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return ret;
}

#endif