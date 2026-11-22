/* sh: a small interactive shell.
 *
 * Reads a line with echo and Backspace, splits it into words and either runs
 * a builtin or starts /bin/<command> (or the given path) with spawn() and
 * waits for it. A trailing '&' runs the program in the background. */

#include <ulib.h>

#define LINE_MAX 128
#define MAX_ARGS 16
#define MAX_BG 8

static int last_status = 0;
static int bg_pids[MAX_BG];

/* ---------------------------------------------------------------- */
/* Line input: the kernel returns raw keys, so echo and editing live here */
static int read_line(char *buf, int max) {
    int len = 0;
    for (;;) {
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) return -1;

        if (c == '\n' || c == '\r') {
            write(STDOUT_FILENO, "\n", 1);
            buf[len] = '\0';
            return len;
        } else if (c == '\b' || c == 127) {
            if (len > 0) {
                len--;
                write(STDOUT_FILENO, "\b \b", 3);
            }
        } else if (c >= 32 && c < 127 && len < max - 1) {
            buf[len++] = c;
            write(STDOUT_FILENO, &c, 1);
        }
    }
}

static int split(char *line, char **argv, int max) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max - 1) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = NULL;
    return argc;
}

/* ---------------------------------------------------------------- */
/* Background jobs: collect the ones that finished */
static void reap_background(void) {
    for (int i = 0; i < MAX_BG; i++) {
        if (!bg_pids[i]) continue;
        int status;
        int r = waitpid(bg_pids[i], &status, WNOHANG);
        if (r > 0) {
            printf("[%d] done (status %d)\n", bg_pids[i], status);
            bg_pids[i] = 0;
        } else if (r < 0) {
            bg_pids[i] = 0;      /* gone (killed and reaped elsewhere) */
        }
    }
}

static void remember_background(int pid) {
    for (int i = 0; i < MAX_BG; i++) {
        if (!bg_pids[i]) {
            bg_pids[i] = pid;
            return;
        }
    }
}

/* ---------------------------------------------------------------- */
/* Builtins */
static void builtin_help(void) {
    puts("Builtins: help, cd [dir], pwd, echo ..., clear, uptime, exit [code]");
    puts("Programs (in /bin): ls, cat, hello, ps, kill, sleep, spin, crash");
    puts("Add '&' to run a program in the background, e.g.: spin &");
}

static int run_builtin(int argc, char **argv) {
    if (!strcmp(argv[0], "help")) {
        builtin_help();
    } else if (!strcmp(argv[0], "exit")) {
        exit(argc > 1 ? atoi(argv[1]) : last_status);
    } else if (!strcmp(argv[0], "cd")) {
        const char *dir = argc > 1 ? argv[1] : "/";
        if (chdir(dir) < 0) printf("cd: %s: no such directory\n", dir);
    } else if (!strcmp(argv[0], "pwd")) {
        char cwd[96];
        if (getcwd(cwd, sizeof(cwd)) >= 0) puts(cwd);
    } else if (!strcmp(argv[0], "echo")) {
        for (int i = 1; i < argc; i++) {
            printf("%s%s", argv[i], i + 1 < argc ? " " : "");
        }
        putchar('\n');
    } else if (!strcmp(argv[0], "clear")) {
        clear_screen();
    } else if (!strcmp(argv[0], "uptime")) {
        unsigned long ms = uptime_ms();
        printf("up %lu.%03lu s\n", ms / 1000, ms % 1000);
    } else {
        return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------- */
static void run_external(int argc, char **argv, int background) {
    (void)argc;
    char path[96];
    if (strchr(argv[0], '/')) {
        strncpy(path, argv[0], sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else {
        snprintf(path, sizeof(path), "/bin/%s", argv[0]);
    }

    int pid = spawn(path, argv);
    if (pid < 0) {
        printf("sh: %s: command not found\n", argv[0]);
        last_status = 127;
        return;
    }

    if (background) {
        printf("[%d] started\n", pid);
        remember_background(pid);
        return;
    }

    int status = 0;
    waitpid(pid, &status, 0);
    last_status = status;
    if (status >= 128) printf("sh: %s terminated (status %d)\n", argv[0], status);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    puts("Loop-OS shell. Type 'help' for commands.");

    for (;;) {
        reap_background();

        char cwd[96];
        if (getcwd(cwd, sizeof(cwd)) < 0) strcpy(cwd, "?");
        printf("loop-os:%s$ ", cwd);

        char line[LINE_MAX];
        if (read_line(line, sizeof(line)) < 0) return 1;

        char *args[MAX_ARGS];
        int n = split(line, args, MAX_ARGS);
        if (n == 0) continue;

        int background = 0;
        if (n > 0 && !strcmp(args[n - 1], "&")) {
            background = 1;
            args[--n] = NULL;
            if (n == 0) continue;
        }

        if (!run_builtin(n, args)) run_external(n, args, background);
    }
}
