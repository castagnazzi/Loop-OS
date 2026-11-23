/* sh: a small interactive shell.
 *
 * Reads a line with echo and Backspace and runs it with fork() and
 * execve(). Supports pipelines (a | b), redirections (< file, > file,
 * >> file) and a trailing '&' for background jobs. */

#include <ulib.h>

#define LINE_MAX 128
#define MAX_ARGS 16
#define MAX_CMDS 6
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
        } else if ((unsigned char)c >= 32 && (unsigned char)c != 127 && len < max - 1) {
            buf[len++] = c;
            write(STDOUT_FILENO, &c, 1);
        }
    }
}

/* ---------------------------------------------------------------- */
/* One command of a pipeline */
struct cmd {
    char *argv[MAX_ARGS];
    int argc;
    char *in_file;
    char *out_file;
    int append;
};

/* Split a line into commands. Operators ('|', '<', '>', '>>') are
 * recognised even without surrounding spaces. Returns the number of
 * commands, or -1 on a syntax error. */
static int parse(char *line, struct cmd *cmds, int *background) {
    int n = 0;
    memset(&cmds[0], 0, sizeof(cmds[0]));
    char *p = line;
    *background = 0;

    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;

        struct cmd *c = &cmds[n];
        if (*p == '|') {
            *p++ = '\0';
            if (c->argc == 0 || ++n >= MAX_CMDS) return -1;
            memset(&cmds[n], 0, sizeof(cmds[n]));
            continue;
        }
        if (*p == '<' || *p == '>') {
            char op = *p;
            *p++ = '\0';
            if (op == '>' && *p == '>') { c->append = 1; *p++ = '\0'; }
            while (*p == ' ') p++;
            if (!*p || *p == '|' || *p == '<' || *p == '>') return -1;
            char *name = p;
            while (*p && *p != ' ' && *p != '|' && *p != '<' && *p != '>') p++;
            if (*p == ' ') *p++ = '\0';
            if (op == '<') c->in_file = name; else c->out_file = name;
            continue;
        }
        if (*p == '&' && (p[1] == '\0' || p[1] == ' ')) {
            *p++ = '\0';
            *background = 1;
            continue;
        }

        if (c->argc >= MAX_ARGS - 1) return -1;
        c->argv[c->argc++] = p;
        while (*p && *p != ' ' && *p != '|' && *p != '<' && *p != '>') p++;
        if (*p == ' ') *p++ = '\0';
    }
    if (n == 0 && cmds[0].argc == 0) return 0;
    if (cmds[n].argc == 0) return -1;
    return n + 1;
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
    puts("Programs (in /bin): ls cat cp mkdir rm wc hello ps kill sleep spin crash");
    puts("Pipes and redirection: ls / | wc    echo hi > /tmp/a    cat < /tmp/a");
    puts("Add '&' to run in the background, e.g.: spin &");
}

/* Returns 1 if argv[0] was a builtin (and runs it) */
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
        write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
    } else if (!strcmp(argv[0], "uptime")) {
        unsigned long ms = uptime_ms();
        printf("up %lu.%03lu s\n", ms / 1000, ms % 1000);
    } else {
        return 0;
    }
    return 1;
}

static int is_builtin(const char *name) {
    static const char *names[] = { "help", "exit", "cd", "pwd", "echo", "clear", "uptime" };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (!strcmp(name, names[i])) return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* Runs in the child process: wire up redirections and become the program */
static void child_exec(struct cmd *c, int in_fd, int out_fd) {
    if (in_fd >= 0) { dup2(in_fd, STDIN_FILENO); }
    if (out_fd >= 0) { dup2(out_fd, STDOUT_FILENO); }

    if (c->in_file) {
        int fd = open(c->in_file, O_RDONLY);
        if (fd < 0) { printf("sh: %s: no such file\n", c->in_file); exit(1); }
        dup2(fd, STDIN_FILENO);
    }
    if (c->out_file) {
        int fd = open(c->out_file, O_WRONLY | O_CREAT | (c->append ? O_APPEND : O_TRUNC));
        if (fd < 0) { printf("sh: cannot write %s\n", c->out_file); exit(1); }
        dup2(fd, STDOUT_FILENO);
    }
    for (int fd = 3; fd < 16; fd++) close(fd);

    if (is_builtin(c->argv[0])) {
        run_builtin(c->argc, c->argv);
        exit(0);
    }

    char path[96];
    if (strchr(c->argv[0], '/')) {
        strncpy(path, c->argv[0], sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else {
        snprintf(path, sizeof(path), "/bin/%s", c->argv[0]);
    }
    execve(path, c->argv);
    printf("sh: %s: command not found\n", c->argv[0]);
    exit(127);
}

static void run_line(struct cmd *cmds, int n, int background) {
    /* A lone builtin without redirection runs in the shell itself (cd, exit) */
    if (n == 1 && !cmds[0].in_file && !cmds[0].out_file && is_builtin(cmds[0].argv[0])) {
        run_builtin(cmds[0].argc, cmds[0].argv);
        return;
    }

    int pids[MAX_CMDS];
    int prev_read = -1;
    for (int i = 0; i < n; i++) {
        int fds[2] = { -1, -1 };
        if (i + 1 < n && pipe(fds) < 0) {
            puts("sh: cannot create pipe");
            break;
        }
        int pid = fork();
        if (pid == 0) {
            if (fds[0] >= 0) close(fds[0]);
            child_exec(&cmds[i], prev_read, fds[1]);
        }
        if (prev_read >= 0) close(prev_read);
        if (fds[1] >= 0) close(fds[1]);
        prev_read = fds[0];
        pids[i] = pid;
        if (pid < 0) puts("sh: fork failed");
    }
    if (prev_read >= 0) close(prev_read);

    if (background) {
        printf("[%d] started\n", pids[n - 1]);
        remember_background(pids[n - 1]);
        return;
    }

    for (int i = 0; i < n; i++) {
        if (pids[i] <= 0) continue;
        int status = 0;
        waitpid(pids[i], &status, 0);
        if (i == n - 1) {
            last_status = status;
            if (status >= 128 && status != 127) {
                printf("sh: %s terminated (status %d)\n", cmds[i].argv[0], status);
            }
        }
    }
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

        struct cmd cmds[MAX_CMDS];
        int background;
        int n = parse(line, cmds, &background);
        if (n == 0) continue;
        if (n < 0) {
            puts("sh: syntax error");
            continue;
        }
        run_line(cmds, n, background);
    }
}
