/* Tasks and the scheduler */

#include <kernel/task.h>
#include <kernel/elf.h>
#include <kernel/fs/vfs.h>
#include <kernel/arch/x86_64/gdt.h>
#include <kernel/arch/x86_64/syscall.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/drivers/timer.h>
#include <kernel/mm/vmm.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

#define KSTACK_PAGES 4
#define TIMESLICE_TICKS 5          /* 50 ms at 100 Hz */
#define ENOENT  2
#define EAGAIN 11
#define ENOMEM 12
#define ENOEXEC 8
#define ECHILD 10
#define EINVAL 22

extern void switch_to(uint64_t *old_rsp_save, uint64_t new_rsp);
extern void user_entry_stub(void);
extern void fork_return_stub(void);

static struct task tasks[MAX_TASKS];
struct task *current_task = NULL;
static int next_pid = 1;
static int slice_ticks = 0;

/* ---------------------------------------------------------------- */
void task_init(void) {
    memset(tasks, 0, sizeof(tasks));
    struct task *t = &tasks[0];
    t->pid = 0;
    t->ppid = -1;
    t->state = T_RUNNING;
    strcpy(t->name, "kernel");
    strcpy(t->cwd, "/");
    current_task = t;
}

/* ---------------------------------------------------------------- */
/* Relations between tasks */
static int has_child(struct task *me, int pid) {
    for (int i = 0; i < MAX_TASKS; i++) {
        struct task *t = &tasks[i];
        if (t->state != T_UNUSED && t != me && t->ppid == me->pid &&
            (pid == -1 || t->pid == pid)) return 1;
    }
    return 0;
}

static struct task *find_zombie_child(struct task *me, int pid) {
    for (int i = 0; i < MAX_TASKS; i++) {
        struct task *t = &tasks[i];
        if (t->state == T_ZOMBIE && t != me && t->ppid == me->pid &&
            (pid == -1 || t->pid == pid)) return t;
    }
    return NULL;
}

/* Release what a zombie still holds (its kernel stack) and free the slot */
static void reap(struct task *t) {
    if (t->space) {
        vmm_space_destroy(t->space);
        t->space = NULL;
    }
    if (t->kstack) {
        vmm_free(t->kstack, KSTACK_PAGES);
        t->kstack = NULL;
    }
    t->state = T_UNUSED;
}

static void reap_orphans(void) {
    for (int i = 1; i < MAX_TASKS; i++) {
        struct task *t = &tasks[i];
        if (t->state == T_ZOMBIE && t->ppid == -1 && t != current_task) reap(t);
    }
}

/* ---------------------------------------------------------------- */
/* Scheduler */
static int ready_now(struct task *t) {
    switch (t->state) {
    case T_READY:
    case T_RUNNING:
        return 1;
    case T_BLOCKED:
        switch (t->wait) {
        case W_KEY:
            if (keyboard_has_key()) goto wake;
            return 0;
        case W_SLEEP:
            if (timer_ticks() >= t->wake_tick) goto wake;
            return 0;
        case W_CHILD:
            /* Wake when a child can be collected, or when none is left */
            if (find_zombie_child(t, t->wait_pid) || !has_child(t, t->wait_pid)) goto wake;
            return 0;
        default:
            goto wake;
        }
    default:
        return 0;
    }
wake:
    t->state = T_READY;
    t->wait = W_NONE;
    return 1;
}

static void switch_task(struct task *prev, struct task *next) {
    if (prev->state == T_RUNNING) prev->state = T_READY;
    next->state = T_RUNNING;
    current_task = next;

    if (next->space) {
        gdt_set_kernel_stack(next->kstack_top);
        syscall_kernel_rsp = next->kstack_top;
        vmm_space_switch(next->space);
    } else {
        vmm_switch_kernel();
    }
    switch_to(&prev->rsp, next->rsp);
}

void schedule(void) {
    struct task *prev = current_task;

    for (;;) {
        reap_orphans();

        int pi = (int)(prev - tasks);
        for (int i = 1; i <= MAX_TASKS; i++) {
            struct task *t = &tasks[(pi + i) % MAX_TASKS];
            if (!ready_now(t)) continue;
            if (t == prev) {
                prev->state = T_RUNNING;      /* nobody else wants the CPU */
                return;
            }
            switch_task(prev, t);
            return;                            /* resumed later */
        }

        /* Nothing can run: wait for an interrupt (keyboard, timer) */
        asm volatile("sti; hlt; cli" : : : "memory");
    }
}

void task_yield(void) {
    uint64_t f = interrupts_save();
    schedule();
    interrupts_restore(f);
}

void task_block(enum wait_kind kind, int arg) {
    uint64_t f = interrupts_save();
    struct task *me = current_task;
    me->state = T_BLOCKED;
    me->wait = kind;
    me->wait_pid = arg;
    if (kind == W_SLEEP) me->wake_tick = timer_ticks() + (uint64_t)arg;
    schedule();
    interrupts_restore(f);
}

void task_timer_tick(struct trap_frame *tf) {
    /* Only user code is preempted, never the kernel */
    if (!(tf->cs & 3)) return;
    if (++slice_ticks < TIMESLICE_TICKS) return;
    slice_ticks = 0;
    schedule();
}

/* ---------------------------------------------------------------- */
/* Termination */
static void terminate(struct task *t, int code) {
    t->exit_code = code;
    for (int i = 0; i < MAX_FDS; i++) {
        if (t->fds[i].kind != FD_NONE) fd_release(&t->fds[i]);
    }

    /* Children lose their parent: nobody will wait for them */
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state != T_UNUSED && tasks[i].ppid == t->pid && &tasks[i] != t) {
            tasks[i].ppid = -1;
        }
    }

    /* The user memory can go now (if it is the active space, leave it first;
     * vmm_space_destroy does that) */
    if (t->space) {
        vmm_space_destroy(t->space);
        t->space = NULL;
    }
    t->state = T_ZOMBIE;
}

void task_exit(int code) {
    interrupts_save();
    terminate(current_task, code);
    schedule();
    for (;;) asm volatile("hlt");   /* a zombie is never scheduled again */
}

int task_kill(int pid) {
    if (pid <= 0) return -EINVAL;
    for (int i = 1; i < MAX_TASKS; i++) {
        struct task *t = &tasks[i];
        if (t->pid != pid || t->state == T_UNUSED) continue;
        if (t == current_task) task_exit(137);
        if (t->state != T_ZOMBIE) terminate(t, 137);
        return 0;
    }
    return -ENOENT;
}

int task_wait(int pid, int *status, int nohang) {
    struct task *me = current_task;
    for (;;) {
        struct task *z = find_zombie_child(me, pid);
        if (z) {
            int zp = z->pid;
            if (status) *status = z->exit_code;
            reap(z);
            return zp;
        }
        if (!has_child(me, pid)) return -ECHILD;
        if (nohang) return 0;
        task_block(W_CHILD, pid);
    }
}

int task_info(int index, struct task_info *out) {
    if (index < 0 || index >= MAX_TASKS) return -1;
    struct task *t = &tasks[index];
    if (t->state == T_UNUSED) return -1;
    out->pid = t->pid;
    out->ppid = t->ppid;
    out->state = (int)t->state;
    strcpy(out->name, t->name);
    return 0;
}

/* ---------------------------------------------------------------- */
/* Spawning */
void task_resolve_path(const char *path, char *out, size_t out_max) {
    size_t n = 0;
    if (path[0] != '/') {
        const char *cwd = current_task->cwd;
        while (*cwd && n < out_max - 2) out[n++] = *cwd++;
        if (n == 0 || out[n - 1] != '/') out[n++] = '/';
    }
    while (*path && n < out_max - 1) out[n++] = *path++;
    out[n] = '\0';
}

/* Lay out argc/argv/envp (no environment) at the top of the user stack,
 * the way crt0 expects them. Returns the initial user RSP. */
static uint64_t build_user_stack(vmm_space_t *sp, const char *const *argv, int argc) {
    uint64_t top = USER_STACK_TOP;
    uint64_t addr[16];
    if (argc > 15) argc = 15;

    for (int i = argc - 1; i >= 0; i--) {
        size_t len = strlen(argv[i]) + 1;
        top -= len;
        vmm_space_copy_to(sp, top, argv[i], len);
        addr[i] = top;
    }

    top &= ~(uint64_t)15;
    size_t words = 1 + (size_t)argc + 1 + 1;      /* argc, argv[], NULL, envp NULL */
    top -= words * 8;
    top &= ~(uint64_t)15;

    uint64_t vec[19];
    vec[0] = (uint64_t)argc;
    for (int i = 0; i < argc; i++) vec[1 + i] = addr[i];
    vec[1 + argc] = 0;
    vec[2 + argc] = 0;
    vmm_space_copy_to(sp, top, vec, words * 8);
    return top;
}

/* Load an executable and build a fresh user stack for it. */
static int build_image(const char *abs, const char *const *argv, int argc,
                       vmm_space_t **sp_out, uint64_t *entry, uint64_t *brk, uint64_t *user_rsp) {
    struct vnode *node = vfs_lookup(abs);
    if (!node || node->is_dir) return -ENOENT;

    vmm_space_t *sp = vmm_space_create();
    if (!sp) return -ENOMEM;

    if (elf_load(sp, node->data, node->size, entry, brk) != 0) {
        vmm_space_destroy(sp);
        return -ENOEXEC;
    }

    const uint64_t stack_flags = VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER | VMM_FLAG_NX;
    for (int p = 0; p < USER_STACK_PAGES; p++) {
        if (vmm_space_alloc_page(sp, USER_STACK_TOP - (uint64_t)(p + 1) * VMM_PAGE_SIZE,
                                 stack_flags) != 0) {
            vmm_space_destroy(sp);
            return -ENOMEM;
        }
    }
    *user_rsp = build_user_stack(sp, argv, argc);
    *sp_out = sp;
    return 0;
}

static void set_name(struct task *t, const char *abs) {
    const char *base = abs;
    for (const char *c = abs; *c; c++) if (*c == '/') base = c + 1;
    memset(t->name, 0, sizeof(t->name));
    strncpy(t->name, base, TASK_NAME_MAX - 1);
}

static int free_slot(void) {
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == T_UNUSED) return i;
    }
    return -1;
}

/* Fresh tasks inherit stdin/stdout/stderr from the parent (so pipes and
 * redirections survive spawn); the boot task has none and uses the console. */
static void inherit_std_fds(struct task *t) {
    for (int i = 0; i < 3; i++) {
        struct fd *src = &current_task->fds[i];
        if (src->kind != FD_NONE) {
            fd_dup_into(&t->fds[i], src);
        } else {
            t->fds[i].kind = i == 0 ? FD_CONSOLE_IN : FD_CONSOLE_OUT;
        }
    }
}

int task_spawn(const char *path, const char *const *argv, int argc) {
    char abs[PATH_BUF];
    task_resolve_path(path, abs, sizeof(abs));

    int slot = free_slot();
    if (slot < 0) return -EAGAIN;

    vmm_space_t *sp;
    uint64_t entry, brk, user_rsp;
    int r = build_image(abs, argv, argc, &sp, &entry, &brk, &user_rsp);
    if (r != 0) return r;

    void *ks = vmm_alloc(KSTACK_PAGES, VMM_KERNEL_FLAGS | VMM_FLAG_NX);
    if (!ks) {
        vmm_space_destroy(sp);
        return -ENOMEM;
    }
    uint64_t ktop = (uint64_t)ks + KSTACK_PAGES * VMM_PAGE_SIZE;

    /* Build the kernel stack so that the first switch_to() "returns" into
     * user_entry_stub, which irets to ring 3 at the program's entry point:
     *   [high] ss rsp rflags cs rip | ret=user_entry_stub | 6 saved regs [low] */
    uint64_t *k = (uint64_t *)ktop;
    *--k = GDT_USER_DATA | 3;
    *--k = user_rsp;
    *--k = 0x202;                       /* IF set */
    *--k = GDT_USER_CODE | 3;
    *--k = entry;
    *--k = (uint64_t)user_entry_stub;
    for (int i = 0; i < 6; i++) *--k = 0;

    struct task *t = &tasks[slot];
    memset(t, 0, sizeof(*t));
    t->pid = next_pid++;
    t->ppid = current_task->pid;
    t->space = sp;
    t->kstack = ks;
    t->kstack_top = ktop;
    t->rsp = (uint64_t)k;
    t->brk_start = t->brk = brk;
    strcpy(t->cwd, current_task->cwd);
    set_name(t, abs);
    inherit_std_fds(t);

    t->state = T_READY;
    return t->pid;
}

int task_fork(const struct syscall_frame *f) {
    struct task *parent = current_task;
    if (!parent->space) return -EINVAL;
    int slot = free_slot();
    if (slot < 0) return -EAGAIN;

    vmm_space_t *sp = vmm_space_clone(parent->space);
    if (!sp) return -ENOMEM;

    void *ks = vmm_alloc(KSTACK_PAGES, VMM_KERNEL_FLAGS | VMM_FLAG_NX);
    if (!ks) {
        vmm_space_destroy(sp);
        return -ENOMEM;
    }
    uint64_t ktop = (uint64_t)ks + KSTACK_PAGES * VMM_PAGE_SIZE;

    /* [high] syscall frame copy (rax = 0) | ret=fork_return_stub | 6 regs [low] */
    uint64_t *k = (uint64_t *)ktop;
    k -= sizeof(struct syscall_frame) / 8;
    memcpy(k, f, sizeof(struct syscall_frame));
    ((struct syscall_frame *)k)->rax = 0;
    *--k = (uint64_t)fork_return_stub;
    for (int i = 0; i < 6; i++) *--k = 0;

    struct task *t = &tasks[slot];
    memset(t, 0, sizeof(*t));
    t->pid = next_pid++;
    t->ppid = parent->pid;
    t->space = sp;
    t->kstack = ks;
    t->kstack_top = ktop;
    t->rsp = (uint64_t)k;
    t->brk_start = parent->brk_start;
    t->brk = parent->brk;
    strcpy(t->cwd, parent->cwd);
    memcpy(t->name, parent->name, sizeof(t->name));
    for (int i = 0; i < MAX_FDS; i++) {
        if (parent->fds[i].kind != FD_NONE) fd_dup_into(&t->fds[i], &parent->fds[i]);
    }

    t->state = T_READY;
    return t->pid;
}

int task_exec(struct syscall_frame *f, const char *path, const char *const *argv, int argc) {
    char abs[PATH_BUF];
    task_resolve_path(path, abs, sizeof(abs));

    vmm_space_t *sp;
    uint64_t entry, brk, user_rsp;
    int r = build_image(abs, argv, argc, &sp, &entry, &brk, &user_rsp);
    if (r != 0) return r;

    struct task *t = current_task;
    vmm_space_t *old = t->space;
    t->space = sp;
    vmm_space_switch(sp);
    vmm_space_destroy(old);

    t->brk_start = t->brk = brk;
    set_name(t, abs);

    /* Enter the new program when the syscall returns; no register survives */
    memset(f, 0, sizeof(*f));
    f->rip = entry;
    f->rflags = 0x202;
    f->user_rsp = user_rsp;
    return 0;
}
