/* Minimal in-kernel shell: a prompt, line editing and a few builtins.
 * It is the fallback when there is no /sbin/init (or after init exits), and
 * a handy way to exercise the kernel directly. The real shell is /bin/sh. */

#include <kernel/kshell.h>
#include <kernel/task.h>
#include <kernel/fs/initrd.h>
#include <kernel/drivers/fb.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/drivers/mouse.h>
#include <kernel/drivers/timer.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/mm_test.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

#define LINE_MAX 128
#define TICKS_PER_SEC 100      /* timer_init(100) in kernel_main_higher_half */

/* ---------------------------------------------------------------- */
/* Builtins */
static void cmd_help(void) {
    kprintf("Commands:\n");
    kprintf("  help          this list\n");
    kprintf("  echo <text>   print text\n");
    kprintf("  uptime        time since boot\n");
    kprintf("  mem           memory usage (PMM and heap)\n");
    kprintf("  memtest       run the memory self test again\n");
    kprintf("  ls [path]     list a directory of the initrd\n");
    kprintf("  cat <file>    print a file from the initrd\n");
    kprintf("  run <prog>    run a program from the initrd and wait for it\n");
    kprintf("  ps            list tasks\n");
    kprintf("  mouse         mouse position and buttons\n");
    kprintf("  clear         clear the screen\n");
    kprintf("  version       kernel info\n");
    kprintf("  reboot        restart the machine\n");
}

static void cmd_uptime(void) {
    uint64_t t = timer_ticks();
    kprintf("up %lu.%02lu s (%lu ticks)\n",
            (unsigned long)(t / TICKS_PER_SEC),
            (unsigned long)((t % TICKS_PER_SEC)),
            (unsigned long)t);
}

static void cmd_mem(void) {
    struct heap_stats hs;
    heap_get_stats(&hs);
    kprintf("Physical: %lu KB total, %lu KB used, %lu KB free\n",
            (unsigned long)(pmm_total_pages() * 4),
            (unsigned long)(pmm_used_pages() * 4),
            (unsigned long)(pmm_free_pages() * 4));
    kprintf("Heap:     %lu KB total, %lu KB used, %lu KB free (%lu blocks)\n",
            (unsigned long)(hs.total / 1024), (unsigned long)(hs.used / 1024),
            (unsigned long)(hs.free / 1024), (unsigned long)hs.blocks);
}

static void cmd_ls(const char *path) {
    if (!path || !*path) path = "/";
    struct initrd_node node;
    if (initrd_lookup(path, &node) != 0) {
        kprintf("ls: %s: no such file or directory\n", path);
        return;
    }
    if (!node.is_dir) {
        kprintf("%s (%lu bytes)\n", path, (unsigned long)node.size);
        return;
    }
    char name[64];
    int is_dir;
    for (size_t i = 0; initrd_readdir(path, i, name, sizeof(name), &is_dir) == 0; i++) {
        kprintf("  %s%s\n", name, is_dir ? "/" : "");
    }
}

static void cmd_cat(const char *path) {
    if (!path || !*path) {
        kprintf("usage: cat <file>\n");
        return;
    }
    struct initrd_node node;
    if (initrd_lookup(path, &node) != 0) {
        kprintf("cat: %s: no such file\n", path);
        return;
    }
    if (node.is_dir) {
        kprintf("cat: %s: is a directory\n", path);
        return;
    }
    for (size_t i = 0; i < node.size; i++) kputchar((char)node.data[i]);
    if (node.size && node.data[node.size - 1] != '\n') kputchar('\n');
}

static void cmd_run(char *args) {
    if (!args || !*args) {
        kprintf("usage: run <program> [args]\n");
        return;
    }

    /* Split "prog a b c" into argv */
    static char buf[LINE_MAX];
    const char *argv[16];
    int argc = 0;
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *p = buf;
    while (*p && argc < 15) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }

    int pid = task_spawn(argv[0], argv, argc);
    if (pid < 0) {
        kprintf("run: cannot start %s (error %d)\n", argv[0], -pid);
        return;
    }
    int status = 0;
    task_wait(pid, &status, 0);
    kprintf("[pid %d exited with status %d]\n", pid, status);
}

static void cmd_ps(void) {
    static const char *names[] = { "?", "ready", "running", "blocked", "zombie" };
    kprintf("  PID  PPID  STATE    NAME\n");
    struct task_info ti;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_info(i, &ti) != 0) continue;
        kprintf("  %3d  %4d  %-8s %s\n", ti.pid, ti.ppid, names[ti.state], ti.name);
    }
}

static void cmd_mouse(void) {
    if (!mouse_present()) {
        kprintf("no mouse\n");
        return;
    }
    uint8_t b = mouse_buttons();
    kprintf("mouse at %d,%d buttons: left=%d middle=%d right=%d\n", mouse_x(), mouse_y(),
            !!(b & MOUSE_BTN_LEFT), !!(b & MOUSE_BTN_MIDDLE), !!(b & MOUSE_BTN_RIGHT));
}

static void cmd_reboot(void) {
    kprintf("Rebooting...\n");
    /* Pulse the CPU reset line through the keyboard controller */
    asm volatile("outb %%al, %%dx" : : "a"((uint8_t)0xFE), "d"((uint16_t)0x64));
    for (;;) asm volatile("hlt");
}

static void run_command(char *line) {
    /* Trim leading spaces */
    while (*line == ' ') line++;
    if (!*line) return;

    char *args = line;
    while (*args && *args != ' ') args++;
    if (*args) {
        *args++ = '\0';
        while (*args == ' ') args++;
    }

    if (!strcmp(line, "help")) cmd_help();
    else if (!strcmp(line, "echo")) kprintf("%s\n", args);
    else if (!strcmp(line, "uptime")) cmd_uptime();
    else if (!strcmp(line, "mem")) cmd_mem();
    else if (!strcmp(line, "memtest")) mm_selftest();
    else if (!strcmp(line, "ls")) cmd_ls(args);
    else if (!strcmp(line, "cat")) cmd_cat(args);
    else if (!strcmp(line, "run")) cmd_run(args);
    else if (!strcmp(line, "ps")) cmd_ps();
    else if (!strcmp(line, "mouse")) cmd_mouse();
    else if (!strcmp(line, "clear")) fbcon_clear();
    else if (!strcmp(line, "version")) kprintf("Loop-OS kernel (x86_64), phase 3\n");
    else if (!strcmp(line, "reboot")) cmd_reboot();
    else kprintf("unknown command: %s (try 'help')\n", line);
}

/* ---------------------------------------------------------------- */
void kshell_run(void) {
    char line[LINE_MAX];
    size_t len = 0;

    kprintf("\nLoop-OS kernel shell. Type 'help' for commands.\n");
    kprintf("loop-os> ");

    for (;;) {
        int c = keyboard_getchar();
        if (c < 0) {
            asm volatile("hlt");     /* wake on the next interrupt */
            continue;
        }

        if (c == '\n' || c == '\r') {
            kprintf("\n");
            line[len] = '\0';
            run_command(line);
            len = 0;
            kprintf("loop-os> ");
        } else if (c == '\b' || c == 127) {
            if (len > 0) {
                len--;
                kprintf("\b \b");
            }
        } else if (c >= 32 && c < 127) {
            if (len < LINE_MAX - 1) {
                line[len++] = (char)c;
                kprintf("%c", c);
            }
        }
    }
}
