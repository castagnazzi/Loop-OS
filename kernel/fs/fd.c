/* File descriptor helpers and pipes */

#include <kernel/task.h>
#include <kernel/fs/vfs.h>
#include <kernel/mm/heap.h>
#include <kernel/drivers/timer.h>
#include <kernel/string.h>

#define PIPE_SIZE 4096

struct pipe {
    uint8_t buf[PIPE_SIZE];
    size_t head, count;
    int readers, writers;
};

void fd_dup_into(struct fd *dst, const struct fd *src) {
    *dst = *src;
    if (src->kind == FD_FILE) vfs_ref(src->vn);
    else if (src->kind == FD_PIPE_R) src->pipe->readers++;
    else if (src->kind == FD_PIPE_W) src->pipe->writers++;
}

void fd_release(struct fd *f) {
    if (f->kind == FD_FILE) {
        vfs_unref(f->vn);
    } else if (f->kind == FD_PIPE_R || f->kind == FD_PIPE_W) {
        struct pipe *p = f->pipe;
        if (f->kind == FD_PIPE_R) p->readers--; else p->writers--;
        if (p->readers <= 0 && p->writers <= 0) kfree(p);
    }
    f->kind = FD_NONE;
}

int pipe_create(struct fd *rd, struct fd *wr) {
    struct pipe *p = kmalloc(sizeof(*p));
    if (!p) return -1;
    memset(p, 0, sizeof(*p));
    p->readers = p->writers = 1;
    memset(rd, 0, sizeof(*rd));
    memset(wr, 0, sizeof(*wr));
    rd->kind = FD_PIPE_R; rd->pipe = p;
    wr->kind = FD_PIPE_W; wr->pipe = p;
    return 0;
}

long pipe_read(struct pipe *p, void *buf, size_t n) {
    while (p->count == 0) {
        if (p->writers <= 0) return 0;
        task_block(W_SLEEP, 1);
    }
    if (n > p->count) n = p->count;
    uint8_t *d = buf;
    for (size_t i = 0; i < n; i++) {
        d[i] = p->buf[p->head];
        p->head = (p->head + 1) % PIPE_SIZE;
    }
    p->count -= n;
    return (long)n;
}

long pipe_write(struct pipe *p, const void *buf, size_t n) {
    const uint8_t *s = buf;
    size_t done = 0;
    while (done < n) {
        if (p->readers <= 0) return done ? (long)done : -32;
        if (p->count == PIPE_SIZE) { task_block(W_SLEEP, 1); continue; }
        p->buf[(p->head + p->count) % PIPE_SIZE] = s[done++];
        p->count++;
    }
    return (long)done;
}
