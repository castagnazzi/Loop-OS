#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

#define MAX_ARGS 64
#define MAX_LINE 1024
#define MAX_PATH 256

typedef struct {
    const char *name;
    int (*func)(int argc, char *argv[]);
    const char *help;
} builtin_t;

/* Builtin declarations */
int builtin_cd(int argc, char *argv[]);
int builtin_pwd(int argc, char *argv[]);
int builtin_echo(int argc, char *argv[]);
int builtin_exit(int argc, char *argv[]);
int builtin_ls(int argc, char *argv[]);
int builtin_cat(int argc, char *argv[]);
int builtin_mkdir(int argc, char *argv[]);
int builtin_rm(int argc, char *argv[]);
int builtin_rmdir(int argc, char *argv[]);
int builtin_touch(int argc, char *argv[]);
int builtin_cp(int argc, char *argv[]);
int builtin_mv(int argc, char *argv[]);
int builtin_ps(int argc, char *argv[]);
int builtin_kill(int argc, char *argv[]);
int builtin_help(int argc, char *argv[]);
int builtin_clear(int argc, char *argv[]);
int builtin_export(int argc, char *argv[]);
int builtin_unset(int argc, char *argv[]);
int builtin_env(int argc, char *argv[]);

/* Builtin table */
static builtin_t builtins[] = {
    {"cd", builtin_cd, "cd <dir> - Change directory"},
    {"pwd", builtin_pwd, "pwd - Print working directory"},
    {"echo", builtin_echo, "echo [args...] - Print arguments"},
    {"exit", builtin_exit, "exit [code] - Exit shell"},
    {"ls", builtin_ls, "ls [dir] - List directory"},
    {"cat", builtin_cat, "cat <file> - Print file contents"},
    {"mkdir", builtin_mkdir, "mkdir <dir> - Create directory"},
    {"rm", builtin_rm, "rm <file> - Remove file"},
    {"rmdir", builtin_rmdir, "rmdir <dir> - Remove empty directory"},
    {"touch", builtin_touch, "touch <file> - Create empty file"},
    {"cp", builtin_cp, "cp <src> <dst> - Copy file"},
    {"mv", builtin_mv, "mv <src> <dst> - Move/rename file"},
    {"ps", builtin_ps, "ps - List processes"},
    {"kill", builtin_kill, "kill <pid> - Send signal to process"},
    {"help", builtin_help, "help - Show this help"},
    {"clear", builtin_clear, "clear - Clear screen"},
    {"export", builtin_export, "export VAR=val - Set environment variable"},
    {"unset", builtin_unset, "unset VAR - Unset environment variable"},
    {"env", builtin_env, "env - Show environment variables"},
    {NULL, NULL, NULL}
};

/* Find builtin by name */
static builtin_t *find_builtin(const char *name) {
    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(builtins[i].name, name) == 0) {
            return &builtins[i];
        }
    }
    return NULL;
}

/* Parse line into argc/argv */
int parse_line(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *p = line;
    
    while (*p && argc < max_args - 1) {
        /* Skip whitespace */
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
        if (!*p) break;
        
        argv[argc++] = p;
        
        /* Find end of token */
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = NULL;
    return argc;
}

/* Execute external command */
int execute_external(char *argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    
    if (pid == 0) {
        /* Child - search PATH */
        const char *path = getenv("PATH");
        if (!path) path = "/bin:/usr/bin";
        
        char cmd_path[256];
        char *path_copy = strdup(path);
        char *dir = strtok(path_copy, ":");
        
        while (dir) {
            snprintf(cmd_path, sizeof(cmd_path), "%s/%s", dir, argv[0]);
            execv(cmd_path, argv);
            dir = strtok(NULL, ":");
        }
        
        /* Try direct path */
        execv(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }
    
    /* Parent waits */
    int status;
    waitpid(-1, &status, 0);
    return WEXITSTATUS(status);
}

/* ===== Builtin implementations ===== */

int builtin_cd(int argc, char *argv[]) {
    const char *dir = argc > 1 ? argv[1] : getenv("HOME");
    if (!dir) dir = "/";
    
    if (chdir(dir) < 0) {
        perror("cd");
        return 1;
    }
    return 0;
}

int builtin_pwd(int argc, char *argv[]) {
    (void)argc; (void)argv;
    char cwd[256];
    if (getcwd(cwd, sizeof(cwd))) {
        puts(cwd);
        return 0;
    }
    perror("pwd");
    return 1;
}

int builtin_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) putchar(' ');
        fputs(argv[i], stdout);
    }
    putchar('\n');
    return 0;
}

int builtin_exit(int argc, char *argv[]) {
    int code = argc > 1 ? atoi(argv[1]) : 0;
    exit(code);
}

int builtin_ls(int argc, char *argv[]) {
    const char *path = argc > 1 ? argv[1] : ".";
    
    DIR *d = opendir(path);
    if (!d) {
        perror("ls");
        return 1;
    }
    
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] != '.') {
            puts(ent->d_name);
        }
    }
    closedir(d);
    return 0;
}

int builtin_cat(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: cat <file>\n");
        return 1;
    }
    
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (!f) {
            perror(argv[i]);
            return 1;
        }
        
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            write(STDOUT_FILENO, buf, n);
        }
        fclose(f);
    }
    return 0;
}

int builtin_mkdir(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mkdir <dir>\n");
        return 1;
    }
    
    for (int i = 1; i < argc; i++) {
        if (mkdir(argv[i], 0755) < 0) {
            perror(argv[i]);
            return 1;
        }
    }
    return 0;
}

int builtin_rm(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: rm <file>\n");
        return 1;
    }
    
    for (int i = 1; i < argc; i++) {
        if (unlink(argv[i]) < 0) {
            perror(argv[i]);
            return 1;
        }
    }
    return 0;
}

int builtin_rmdir(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: rmdir <dir>\n");
        return 1;
    }
    
    for (int i = 1; i < argc; i++) {
        if (rmdir(argv[i]) < 0) {
            perror(argv[i]);
            return 1;
        }
    }
    return 0;
}

int builtin_touch(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: touch <file>\n");
        return 1;
    }
    
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_CREAT | O_WRONLY, 0644);
        if (fd < 0) {
            perror(argv[i]);
            return 1;
        }
        close(fd);
    }
    return 0;
}

int builtin_cp(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: cp <src> <dst>\n");
        return 1;
    }
    
    int src_fd = open(argv[1], O_RDONLY);
    if (src_fd < 0) { perror(argv[1]); return 1; }
    
    int dst_fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) { perror(argv[2]); close(src_fd); return 1; }
    
    char buf[4096];
    ssize_t n;
    while ((n = read(src_fd, buf, sizeof(buf))) > 0) {
        if (write(dst_fd, buf, n) != n) { perror("write"); break; }
    }
    
    close(src_fd);
    close(dst_fd);
    return 0;
}

int builtin_mv(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: mv <src> <dst>\n");
        return 1;
    }
    
    if (rename(argv[1], argv[2]) < 0) {
        perror("mv");
        return 1;
    }
    return 0;
}

int builtin_ps(int argc, char *argv[]) {
    (void)argc; (void)argv;
    puts("PID  PPID  CMD");
    /* TODO: read /proc */
    puts("1    0     init");
    return 0;
}

int builtin_kill(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: kill <pid>\n");
        return 1;
    }
    
    pid_t pid = atoi(argv[1]);
    int sig = argc > 2 ? atoi(argv[2]) : 15; /* SIGTERM */
    
    if (kill(pid, sig) < 0) {
        perror("kill");
        return 1;
    }
    return 0;
}

int builtin_help(int argc, char *argv[]) {
    (void)argc; (void)argv;
    puts("Builtin commands:");
    for (int i = 0; builtins[i].name; i++) {
        printf("  %s - %s\n", builtins[i].name, builtins[i].help);
    }
    return 0;
}

int builtin_clear(int argc, char *argv[]) {
    (void)argc; (void)argv;
    puts("\033[2J\033[H");
    return 0;
}

int builtin_export(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: export VAR=value\n");
        return 1;
    }
    
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            *eq = '\0';
            setenv(argv[i], eq + 1, 1);
        }
    }
    return 0;
}

int builtin_unset(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: unset VAR\n");
        return 1;
    }
    
    for (int i = 1; i < argc; i++) {
        unsetenv(argv[i]);
    }
    return 0;
}

int builtin_env(int argc, char *argv[]) {
    (void)argc; (void)argv;
    extern char **environ;
    for (char **env = environ; *env; env++) {
        puts(*env);
    }
    return 0;
}

/* ===== Main shell loop ===== */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    
    char line[MAX_LINE];
    char *argv_parsed[MAX_ARGS];
    char cwd[256];
    
    puts("=== Loop-OS Shell ===");
    puts("Type 'help' for commands");
    
    while (1) {
        /* Prompt */
        getcwd(cwd, sizeof(cwd));
        printf("\033[1;32m%s\033[0m $ ", cwd);
        fflush(stdout);
        
        /* Read line */
        if (!fgets(line, sizeof(line), stdin)) {
            putchar('\n');
            break;
        }
        
        /* Parse */
        int argc_parsed = parse_line(line, argv_parsed, MAX_ARGS);
        if (argc_parsed == 0) continue;
        
        /* Check builtin */
        builtin_t *b = find_builtin(argv_parsed[0]);
        if (b) {
            b->func(argc_parsed, argv_parsed);
        } else {
            /* External command */
            execute_external(argv_parsed);
        }
    }
    
    puts("bye");
    return 0;
}