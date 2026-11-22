/* Tasks (processes) and the scheduler.
 *
 * Single CPU, cooperative inside the kernel: kernel code is never preempted.
 * A user task is preempted by the timer only while it runs in ring 3; kernel
 * work (syscalls) runs with interrupts disabled until the task blocks, yields
 * or returns to user mode. */

#ifndef _KERNEL_TASK_H
#define _KERNEL_TASK_H

#include <stddef.h>
#include <stdint.h>
#include <kernel/mm/vmm.h>
#include <kernel/arch/x86_64/idt.h>

#define MAX_TASKS 32
#define MAX_FDS   16
#define TASK_NAME_MAX 32
#define PATH_BUF 96

/* User address space layout */
#define USER_STACK_TOP   0x7FFFFFFF000ULL
#define USER_STACK_PAGES 16
#define USER_STACK_LOW   (USER_STACK_TOP - USER_STACK_PAGES * VMM_PAGE_SIZE)
#define USER_LIMIT       0x00007FFFFFFFF000ULL    /* user pointers must be below this */
#define USER_HEAP_MAX    (256ULL * 1024 * 1024)   /* sbrk budget */

enum task_state { T_UNUSED = 0, T_READY, T_RUNNING, T_BLOCKED, T_ZOMBIE };
enum wait_kind { W_NONE = 0, W_KEY, W_CHILD, W_SLEEP };

/* Open file descriptor */
enum fd_kind { FD_NONE = 0, FD_CONSOLE_IN, FD_CONSOLE_OUT, FD_FILE, FD_DIR };
struct fd {
    enum fd_kind kind;
    const uint8_t *data;       /* FD_FILE: contents in the initrd */
    size_t size;
    size_t off;                /* FD_FILE: read offset */
    char path[PATH_BUF];       /* FD_DIR: directory path */
};

struct task {
    int pid;
    int ppid;                  /* -1: orphan */
    enum task_state state;
    char name[TASK_NAME_MAX];

    uint64_t rsp;              /* saved kernel stack pointer while switched out */
    void *kstack;              /* base of the kernel stack allocation */
    uint64_t kstack_top;
    vmm_space_t *space;        /* NULL for the kernel task */

    enum wait_kind wait;
    int wait_pid;              /* W_CHILD: pid to wait for (-1 = any) */
    uint64_t wake_tick;        /* W_SLEEP */

    int exit_code;
    uint64_t brk_start, brk;   /* user heap */

    char cwd[PATH_BUF];
    struct fd fds[MAX_FDS];
};

struct task_info {
    int pid, ppid;
    int state;
    char name[TASK_NAME_MAX];
};

extern struct task *current_task;

/* Set up the boot context as task 0 ("kernel") */
void task_init(void);

/* Load an executable from the initrd and create a ready-to-run task.
 * Returns the new pid, or a negative errno. */
int task_spawn(const char *path, const char *const *argv, int argc);

/* Wait for a child (pid = -1 for any; nohang returns 0 if none has exited
 * yet instead of blocking). Returns its pid and stores the exit
 * status, or a negative errno (-10 ECHILD). */
int task_wait(int pid, int *status, int nohang);

__attribute__((noreturn)) void task_exit(int code);
int task_kill(int pid);

/* Let other tasks run; returns when this task is scheduled again */
void task_yield(void);
/* Block the current task until the condition holds, then return */
void task_block(enum wait_kind kind, int arg);

/* Pick the next runnable task (interrupts must be off). Idles if none. */
void schedule(void);

/* Called from the timer IRQ after the EOI; may switch tasks */
void task_timer_tick(struct trap_frame *tf);

/* Enumerate live tasks (index 0..MAX_TASKS-1). Returns 0 if the slot is used. */
int task_info(int index, struct task_info *out);

/* Build an absolute path from the current task's cwd */
void task_resolve_path(const char *path, char *out, size_t out_max);

#endif
