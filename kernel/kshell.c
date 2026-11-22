/* Minimal in-kernel shell: a prompt, line editing and a few builtins.
 * It exists to exercise the keyboard, timer, console and memory manager
 * until userspace (init + a real shell) is available. */

#include <kernel/kshell.h>
#include <kernel/drivers/fb.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/drivers/mouse.h>
#include <kernel/drivers/timer.h>
#include <kernel/fs/initrd.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/mm_test.h>
#include <kernel/stdio.h>
#include <kernel/string.h>
#include <stdint.h>

#define LINE_MAX 128
#define TICKS_PER_SEC 100      /* timer_init(100) in kernel_main_higher_half */

/* ---------------------------------------------------------------- */
/* Uptime clock in the title bar (proves the timer interrupt works) */
static void draw_uptime(uint64_t secs) {
    if (!fb_ready()) return;
    char buf[32];
    ksnprintf(buf, sizeof(buf), "uptime %02lu:%02lu:%02lu",
              (unsigned long)(secs / 3600), (unsigned long)((secs / 60) % 60),
              (unsigned long)(secs % 60));
    int w = (int)(strlen(buf) * 16);
    int x = (int)fb_width() - w - 24;
    fb_fill_rect(x - 8, 14, w + 16, 28, fb_rgb(24, 28, 44));
    fb_draw_string(x, 20, buf, fb_rgb(150, 220, 170), FB_TRANSPARENT, 2);
}

static void draw_mouse_status(void) {
    if (!fb_ready() || !mouse_present()) return;
    char buf[48];
    uint8_t b = mouse_buttons();
    ksnprintf(buf, sizeof(buf), "mouse %4d,%4d [%c%c%c]", mouse_x(), mouse_y(),
              (b & MOUSE_BTN_LEFT) ? 'L' : '-',
              (b & MOUSE_BTN_MIDDLE) ? 'M' : '-',
              (b & MOUSE_BTN_RIGHT) ? 'R' : '-');
    int w = (int)(strlen(buf) * 8);
    int x = (int)fb_width() - w - 24;
    fb_fill_rect(x - 4, 42, w + 8, 10, fb_rgb(24, 28, 44));
    fb_draw_string(x, 43, buf, fb_rgb(150, 170, 200), FB_TRANSPARENT, 1);
}

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
    else if (!strcmp(line, "mouse")) cmd_mouse();
    else if (!strcmp(line, "clear")) fbcon_clear();
    else if (!strcmp(line, "version")) kprintf("Loop-OS kernel (x86_64), phase 1\n");
    else if (!strcmp(line, "reboot")) cmd_reboot();
    else kprintf("unknown command: %s (try 'help')\n", line);
}

/* ---------------------------------------------------------------- */
void kshell_run(void) {
    char line[LINE_MAX];
    size_t len = 0;
    uint64_t last_sec = (uint64_t)-1;
    uint32_t last_mouse = (uint32_t)-1;

    kprintf("\nLoop-OS shell. Type 'help' for commands.\n");
    kprintf("loop-os> ");

    for (;;) {
        /* Refresh the clock once per second */
        uint64_t sec = timer_ticks() / TICKS_PER_SEC;
        if (sec != last_sec) {
            last_sec = sec;
            draw_uptime(sec);
        }
        uint32_t ms = mouse_serial();
        if (ms != last_mouse) {
            last_mouse = ms;
            draw_mouse_status();
        }

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
