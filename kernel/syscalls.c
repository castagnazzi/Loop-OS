/* System call dispatcher.
 *
 * Runs on the calling task's kernel stack with interrupts disabled (SFMASK
 * clears IF). A call that has to wait blocks the task through the scheduler,
 * which re-enables interrupts only while the CPU is idle. */

#include <kernel/syscall_nums.h>
#include <kernel/task.h>
#include <kernel/fs/vfs.h>
#include <kernel/arch/x86_64/syscall.h>
#include <kernel/drivers/fb.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/drivers/timer.h>
#include <kernel/mm/vmm.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

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
    long n = 0;

    if (f->kind == FD_CONSOLE_IN) {
        while (!keyboard_has_key()) task_block(W_KEY, 0);
        while ((uint64_t)n < count && keyboard_has_key()) {
            int c = keyboard_getchar();
            if (c < 0) break;
            tmp[n++] = (uint8_t)c;
        }
    } else if (f->kind == FD_FILE) {
        n = vfs_read(f->vn, f->off, tmp, count);
        if (n > 0) f->off += (size_t)n;
    } else if (f->kind == FD_PIPE_R) {
        n = pipe_read(f->pipe, tmp, count);
    } else {
        return -SYS_EBADF;
    }

    if (n > 0 && copy_to_user(buf, tmp, (size_t)n) != 0) return -SYS_EFAULT;
    return n;
}

static long sys_write(int fdn, uint64_t buf, uint64_t count) {
    struct fd *f = get_fd(fdn);
    if (!f) return -SYS_EBADF;
    if (f->kind != FD_CONSOLE_OUT && f->kind != FD_FILE && f->kind != FD_PIPE_W) return -SYS_EACCES;
    if (f->kind == FD_FILE && !(f->flags & (O_WRONLY | O_RDWR))) return -SYS_EACCES;

    if (f->kind == FD_FILE && (f->flags & O_APPEND)) f->off = f->vn->size;

    uint64_t done = 0;
    while (done < count) {
        uint8_t tmp[MAX_IO_CHUNK];
        size_t chunk = count - done < MAX_IO_CHUNK ? (size_t)(count - done) : MAX_IO_CHUNK;
        if (copy_from_user(tmp, buf + done, chunk) != 0) return done ? (long)done : -SYS_EFAULT;
        if (f->kind == FD_CONSOLE_OUT) {
            for (size_t i = 0; i < chunk; i++) kputchar((char)tmp[i]);
        } else if (f->kind == FD_FILE) {
            long w = vfs_write(f->vn, f->off, tmp, chunk);
            if (w < 0) return done ? (long)done : w;
            f->off += (size_t)w;
        } else {
            long w = pipe_write(f->pipe, tmp, chunk);
            if (w < 0) return done ? (long)done : w;
        }
        done += chunk;
    }
    return (long)done;
}

static struct fd *alloc_fd(int *idx) {
    for (int i = 3; i < MAX_FDS; i++) {
        if (current_task->fds[i].kind == FD_NONE) {
            *idx = i;
            return &current_task->fds[i];
        }
    }
    return NULL;
}

static long sys_open(uint64_t upath, uint64_t flags) {
    char path[PATH_BUF], abs[PATH_BUF];
    if (copy_str_from_user(path, upath, sizeof(path)) != 0) return -SYS_EFAULT;

    task_resolve_path(path, abs, sizeof(abs));
    struct vnode *n = vfs_lookup(abs);
    if (!n && (flags & O_CREAT)) n = vfs_create(abs, 0);
    if (!n) return -SYS_ENOENT;

    int idx;
    struct fd *f = alloc_fd(&idx);
    if (!f) return -SYS_EMFILE;
    memset(f, 0, sizeof(*f));
    if (n->is_dir) {
        if (flags & (O_WRONLY | O_RDWR)) return -SYS_EACCES;
        f->kind = FD_DIR;
        vfs_canonical(abs, f->path, sizeof(f->path));
        f->vn = n;
    } else {
        f->kind = FD_FILE;
        f->vn = n;
        f->flags = (int)flags;
        vfs_ref(n);
        if (flags & O_TRUNC) vfs_truncate(n);
    }
    return idx;
}

static long sys_close(int fdn) {
    if (fdn < 0 || fdn >= MAX_FDS) return -SYS_EBADF;
    struct fd *f = &current_task->fds[fdn];
    if (f->kind == FD_NONE) return -SYS_EBADF;
    fd_release(f);
    return 0;
}

static long sys_lseek(int fdn, int64_t off, int whence) {
    struct fd *f = get_fd(fdn);
    if (!f || f->kind != FD_FILE) return -SYS_EBADF;
    int64_t base;
    switch (whence) {
    case 0: base = 0; break;
    case 1: base = (int64_t)f->off; break;
    case 2: base = (int64_t)f->vn->size; break;
    default: return -SYS_EINVAL;
    }
    int64_t pos = base + off;
    if (pos < 0) return -SYS_EINVAL;
    f->off = (size_t)pos;
    return (long)f->off;
}

static long sys_readdir(int fdn, uint64_t ubuf) {
    struct fd *f = get_fd(fdn);
    if (!f || f->kind != FD_DIR) return -SYS_EBADF;

    struct vnode *e = vfs_entry_at(f->vn, f->off);
    if (!e) return 0;
    struct sys_dirent d;
    memset(&d, 0, sizeof(d));
    strncpy(d.name, e->name, sizeof(d.name) - 1);
    d.is_dir = (uint32_t)e->is_dir;
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

    struct vnode *n = vfs_lookup(abs);
    if (!n || !n->is_dir) return -SYS_ENOENT;

    vfs_canonical(abs, current_task->cwd, sizeof(current_task->cwd));
    return 0;
}

static long sys_mkdir(uint64_t upath) {
    char path[PATH_BUF], abs[PATH_BUF];
    if (copy_str_from_user(path, upath, sizeof(path)) != 0) return -SYS_EFAULT;
    task_resolve_path(path, abs, sizeof(abs));
    return vfs_create(abs, 1) ? 0 : -SYS_EINVAL;
}

static long sys_unlink(uint64_t upath) {
    char path[PATH_BUF], abs[PATH_BUF];
    if (copy_str_from_user(path, upath, sizeof(path)) != 0) return -SYS_EFAULT;
    task_resolve_path(path, abs, sizeof(abs));
    return vfs_unlink(abs);
}

static long sys_pipe(uint64_t ufds) {
    int ri, wi;
    struct fd *r = alloc_fd(&ri);
    if (!r) return -SYS_EMFILE;
    r->kind = FD_PIPE_R;                 /* reserve the slot before the next search */
    struct fd *w = alloc_fd(&wi);
    if (!w) { r->kind = FD_NONE; return -SYS_EMFILE; }
    if (pipe_create(r, w) != 0) { r->kind = FD_NONE; return -SYS_ENOMEM; }
    int out[2] = { ri, wi };
    if (copy_to_user(ufds, out, sizeof(out)) != 0) {
        fd_release(r);
        fd_release(w);
        return -SYS_EFAULT;
    }
    return 0;
}

static long sys_dup2(int oldfd, int newfd) {
    struct fd *src = get_fd(oldfd);
    if (!src) return -SYS_EBADF;
    if (newfd < 0 || newfd >= MAX_FDS) return -SYS_EBADF;
    if (oldfd == newfd) return newfd;
    struct fd *dst = &current_task->fds[newfd];
    if (dst->kind != FD_NONE) fd_release(dst);
    fd_dup_into(dst, src);
    return newfd;
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

static char kargs[MAX_ARGS][ARG_LEN];     /* kernel is not reentrant here */

/* Read a NULL-terminated argv from user memory into kargs */
static long load_args(uint64_t upath, uint64_t uargv, char *path, const char **argv, int *argcp) {
    if (copy_str_from_user(path, upath, PATH_BUF) != 0) return -SYS_EFAULT;
    int argc = 0;
    if (uargv) {
        for (; argc < MAX_ARGS - 1; argc++) {
            uint64_t ptr;
            if (copy_from_user(&ptr, uargv + (uint64_t)argc * 8, 8) != 0) return -SYS_EFAULT;
            if (ptr == 0) break;
            if (copy_str_from_user(kargs[argc], ptr, ARG_LEN) != 0) return -SYS_EFAULT;
            argv[argc] = kargs[argc];
        }
    }
    if (argc == 0) {
        strncpy(kargs[0], path, ARG_LEN - 1);
        kargs[0][ARG_LEN - 1] = '\0';
        argv[0] = kargs[0];
        argc = 1;
    }
    *argcp = argc;
    return 0;
}

static long sys_spawn(uint64_t upath, uint64_t uargv) {
    char path[PATH_BUF];
    const char *argv[MAX_ARGS];
    int argc;
    long r = load_args(upath, uargv, path, argv, &argc);
    if (r < 0) return r;
    return task_spawn(path, argv, argc);
}

static long sys_execve(struct syscall_frame *f, uint64_t upath, uint64_t uargv) {
    char path[PATH_BUF];
    const char *argv[MAX_ARGS];
    int argc;
    long r = load_args(upath, uargv, path, argv, &argc);
    if (r < 0) return r;
    return task_exec(f, path, argv, argc);
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
    case SYS_PIPE:      ret = sys_pipe(a1); break;
    case SYS_DUP2:      ret = sys_dup2((int)a1, (int)a2); break;
    case SYS_FORK:      ret = task_fork(f); break;
    case SYS_EXECVE:    ret = sys_execve(f, a1, a2); break;
    case SYS_MKDIR:     ret = sys_mkdir(a1); break;
    case SYS_UNLINK:    ret = sys_unlink(a1); break;
    default:            ret = -SYS_ENOSYS; break;
    }
    f->rax = (uint64_t)ret;
}
