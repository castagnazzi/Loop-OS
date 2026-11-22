/* System call dispatcher.
 *
 * Runs on the calling task's kernel stack with interrupts disabled (SFMASK
 * clears IF). A call that has to wait blocks the task through the scheduler,
 * which re-enables interrupts only while the CPU is idle. */

#include <kernel/syscall_nums.h>
#include <kernel/task.h>
#include <kernel/fs/initrd.h>
#include <kernel/drivers/fb.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/drivers/timer.h>
#include <kernel/mm/vmm.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

/* Matches the frame pushed by syscall_entry (lowest address first) */
struct syscall_frame {
    uint64_t r15, r14, r13, r12, rbp, rbx, r9, r8, r10, rdx, rsi, rdi;
    uint64_t rax;
    uint64_t rip;        /* RCX */
    uint64_t rflags;     /* R11 */
    uint64_t user_rsp;
};

#define MAX_IO_CHUNK 256
#define MAX_ARGS 16
#define ARG_LEN 64

/* ---------------------------------------------------------------- */
/* User memory access */
static int user_range_ok(uint64_t addr, size_t len) {
    return addr < USER_LIMIT && len <= USER_LIMIT && addr + len <= USER_LIMIT;
}

static int copy_from_user(void *dst, uint64_t src, size_t n) {
    if (!user_range_ok(src, n)) return -1;
    return vmm_space_copy_from(current_task->space, dst, src, n);
}

static int copy_to_user(uint64_t dst, const void *src, size_t n) {
    if (!user_range_ok(dst, n)) return -1;
    return vmm_space_copy_to(current_task->space, dst, src, n);
}

/* Copy a NUL-terminated string; fails if it does not fit in 'max' bytes */
static int copy_str_from_user(char *dst, uint64_t src, size_t max) {
    for (size_t i = 0; i < max; i++) {
        if (copy_from_user(&dst[i], src + i, 1) != 0) return -1;
        if (dst[i] == '\0') return 0;
    }
    dst[max - 1] = '\0';
    return -1;
}

/* ---------------------------------------------------------------- */
static struct fd *get_fd(int fd) {
    if (fd < 0 || fd >= MAX_FDS) return NULL;
    struct fd *f = &current_task->fds[fd];
    return f->kind == FD_NONE ? NULL : f;
}

static long sys_read(int fdn, uint64_t buf, uint64_t count) {
    struct fd *f = get_fd(fdn);
    if (!f) return -SYS_EBADF;
    if (count == 0) return 0;
    if (count > MAX_IO_CHUNK) count = MAX_IO_CHUNK;

    uint8_t tmp[MAX_IO_CHUNK];
    size_t n = 0;

    if (f->kind == FD_CONSOLE_IN) {
        while (!keyboard_has_key()) task_block(W_KEY, 0);
        while (n < count && keyboard_has_key()) {
            int c = keyboard_getchar();
            if (c < 0) break;
            tmp[n++] = (uint8_t)c;
        }
    } else if (f->kind == FD_FILE) {
        size_t left = f->size - f->off;
        n = count < left ? (size_t)count : left;
        memcpy(tmp, f->data + f->off, n);
        f->off += n;
    } else {
        return -SYS_EBADF;
    }

    if (n && copy_to_user(buf, tmp, n) != 0) return -SYS_EFAULT;
    return (long)n;
}

static long sys_write(int fdn, uint64_t buf, uint64_t count) {
    struct fd *f = get_fd(fdn);
    if (!f) return -SYS_EBADF;
    if (f->kind != FD_CONSOLE_OUT) return -SYS_EACCES;

    uint64_t done = 0;
    while (done < count) {
        uint8_t tmp[MAX_IO_CHUNK];
        size_t chunk = count - done < MAX_IO_CHUNK ? (size_t)(count - done) : MAX_IO_CHUNK;
        if (copy_from_user(tmp, buf + done, chunk) != 0) return done ? (long)done : -SYS_EFAULT;
        for (size_t i = 0; i < chunk; i++) kputchar((char)tmp[i]);
        done += chunk;
    }
    return (long)done;
}

static long sys_open(uint64_t upath, uint64_t flags) {
    char path[PATH_BUF], abs[PATH_BUF];
    if (copy_str_from_user(path, upath, sizeof(path)) != 0) return -SYS_EFAULT;
    if (flags & (O_WRONLY | O_RDWR | O_CREAT)) return -SYS_EACCES;   /* read-only fs */

    task_resolve_path(path, abs, sizeof(abs));
    struct initrd_node node;
    if (initrd_lookup(abs, &node) != 0) return -SYS_ENOENT;

    for (int i = 3; i < MAX_FDS; i++) {
        struct fd *f = &current_task->fds[i];
        if (f->kind != FD_NONE) continue;
        memset(f, 0, sizeof(*f));
        if (node.is_dir) {
            f->kind = FD_DIR;
            strncpy(f->path, abs, sizeof(f->path) - 1);
        } else {
            f->kind = FD_FILE;
            f->data = node.data;
            f->size = node.size;
        }
        return i;
    }
    return -SYS_EMFILE;
}

static long sys_close(int fdn) {
    if (fdn < 3) return 0;          /* the standard descriptors stay open */
    struct fd *f = get_fd(fdn);
    if (!f) return -SYS_EBADF;
    f->kind = FD_NONE;
    return 0;
}

static long sys_lseek(int fdn, int64_t off, int whence) {
    struct fd *f = get_fd(fdn);
    if (!f || f->kind != FD_FILE) return -SYS_EBADF;
    int64_t base;
    switch (whence) {
    case 0: base = 0; break;
    case 1: base = (int64_t)f->off; break;
    case 2: base = (int64_t)f->size; break;
    default: return -SYS_EINVAL;
    }
    int64_t pos = base + off;
    if (pos < 0) return -SYS_EINVAL;
    f->off = (size_t)pos > f->size ? f->size : (size_t)pos;
    return (long)f->off;
}

static long sys_readdir(int fdn, uint64_t ubuf) {
    struct fd *f = get_fd(fdn);
    if (!f || f->kind != FD_DIR) return -SYS_EBADF;

    struct sys_dirent d;
    int is_dir = 0;
    memset(&d, 0, sizeof(d));
    if (initrd_readdir(f->path, f->off, d.name, sizeof(d.name), &is_dir) != 0) return 0;
    d.is_dir = (uint32_t)is_dir;
    f->off++;
    if (copy_to_user(ubuf, &d, sizeof(d)) != 0) return -SYS_EFAULT;
    return 1;
}

static long sys_getcwd(uint64_t ubuf, uint64_t size) {
    size_t len = strlen(current_task->cwd) + 1;
    if (size < len) return -SYS_EINVAL;
    if (copy_to_user(ubuf, current_task->cwd, len) != 0) return -SYS_EFAULT;
    return (long)len - 1;
}

static long sys_chdir(uint64_t upath) {
    char path[PATH_BUF], abs[PATH_BUF];
    if (copy_str_from_user(path, upath, sizeof(path)) != 0) return -SYS_EFAULT;
    task_resolve_path(path, abs, sizeof(abs));

    struct initrd_node node;
    if (initrd_lookup(abs, &node) != 0) return -SYS_ENOENT;
    if (!node.is_dir) return -SYS_ENOENT;

    initrd_canonical(abs, current_task->cwd, sizeof(current_task->cwd));
    return 0;
}

static long sys_sbrk(int64_t inc) {
    struct task *t = current_task;
    uint64_t old = t->brk;
    if (inc == 0) return (long)old;

    int64_t new_brk = (int64_t)old + inc;
    if (new_brk < (int64_t)t->brk_start ||
        (uint64_t)new_brk > t->brk_start + USER_HEAP_MAX) return -SYS_ENOMEM;

    /* Map any new pages (shrinking just moves the break; pages stay mapped) */
    uint64_t have = (old + VMM_PAGE_SIZE - 1) & ~(uint64_t)(VMM_PAGE_SIZE - 1);
    uint64_t want = ((uint64_t)new_brk + VMM_PAGE_SIZE - 1) & ~(uint64_t)(VMM_PAGE_SIZE - 1);
    const uint64_t flags = VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER | VMM_FLAG_NX;
    for (uint64_t va = have; va < want; va += VMM_PAGE_SIZE) {
        if (vmm_space_translate(t->space, va)) continue;
        if (vmm_space_alloc_page(t->space, va, flags) != 0) return -SYS_ENOMEM;
    }
    t->brk = (uint64_t)new_brk;
    return (long)old;
}

static long sys_spawn(uint64_t upath, uint64_t uargv) {
    char path[PATH_BUF];
    if (copy_str_from_user(path, upath, sizeof(path)) != 0) return -SYS_EFAULT;

    static char args[MAX_ARGS][ARG_LEN];     /* kernel is not reentrant here */
    const char *argv[MAX_ARGS];
    int argc = 0;

    if (uargv) {
        for (; argc < MAX_ARGS - 1; argc++) {
            uint64_t ptr;
            if (copy_from_user(&ptr, uargv + (uint64_t)argc * 8, 8) != 0) return -SYS_EFAULT;
            if (ptr == 0) break;
            if (copy_str_from_user(args[argc], ptr, ARG_LEN) != 0) return -SYS_EFAULT;
            argv[argc] = args[argc];
        }
    }
    if (argc == 0) {
        strncpy(args[0], path, ARG_LEN - 1);
        args[0][ARG_LEN - 1] = '\0';
        argv[0] = args[0];
        argc = 1;
    }
    return task_spawn(path, argv, argc);
}

static long sys_wait4(int pid, uint64_t ustatus, uint64_t options) {
    int status = 0;
    int r = task_wait(pid, &status, (int)(options & 1));
    if (r <= 0) return r;
    if (ustatus && copy_to_user(ustatus, &status, sizeof(status)) != 0) return -SYS_EFAULT;
    return r;
}

static long sys_taskinfo(int index, uint64_t ubuf) {
    struct task_info ti;
    if (task_info(index, &ti) != 0) return -SYS_ENOENT;
    struct sys_taskinfo out;
    memset(&out, 0, sizeof(out));
    out.pid = ti.pid;
    out.ppid = ti.ppid;
    out.state = ti.state;
    strncpy(out.name, ti.name, sizeof(out.name) - 1);
    if (copy_to_user(ubuf, &out, sizeof(out)) != 0) return -SYS_EFAULT;
    return 0;
}

/* ---------------------------------------------------------------- */
void syscall_dispatch(struct syscall_frame *f) {
    long ret;
    uint64_t a1 = f->rdi, a2 = f->rsi, a3 = f->rdx;

    switch (f->rax) {
    case SYS_READ:      ret = sys_read((int)a1, a2, a3); break;
    case SYS_WRITE:     ret = sys_write((int)a1, a2, a3); break;
    case SYS_OPEN:      ret = sys_open(a1, a2); break;
    case SYS_CLOSE:     ret = sys_close((int)a1); break;
    case SYS_LSEEK:     ret = sys_lseek((int)a1, (int64_t)a2, (int)a3); break;
    case SYS_YIELD:     task_yield(); ret = 0; break;
    case SYS_GETPID:    ret = current_task->pid; break;
    case SYS_EXIT:      task_exit((int)a1);
    case SYS_WAIT4:     ret = sys_wait4((int)a1, a2, a3); break;
    case SYS_KILL:      ret = task_kill((int)a1); break;
    case SYS_READDIR:   ret = sys_readdir((int)a1, a2); break;
    case SYS_GETCWD:    ret = sys_getcwd(a1, a2); break;
    case SYS_CHDIR:     ret = sys_chdir(a1); break;
    case SYS_SBRK:      ret = sys_sbrk((int64_t)a1); break;
    case SYS_SPAWN:     ret = sys_spawn(a1, a2); break;
    case SYS_UPTIME_MS: ret = (long)(timer_micros() / 1000); break;
    case SYS_SLEEP_MS:
        task_block(W_SLEEP, (int)((a1 + 9) / 10));   /* 100 Hz ticks */
        ret = 0;
        break;
    case SYS_CLEAR:     fbcon_clear(); ret = 0; break;
    case SYS_TASKINFO:  ret = sys_taskinfo((int)a1, a2); break;
    default:            ret = -SYS_ENOSYS; break;
    }
    f->rax = (uint64_t)ret;
}
