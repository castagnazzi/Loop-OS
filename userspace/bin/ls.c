/* ls: list directories (default: the current one) */

#include <ulib.h>

static int list(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("ls: %s: no such file or directory\n", path);
        return 1;
    }

    struct sys_dirent d;
    int n = readdir(fd, &d);
    if (n < 0) {
        puts(path);               /* a regular file: just print its name */
        close(fd);
        return 0;
    }
    while (n > 0) {
        printf("%s%s\n", d.name, d.is_dir ? "/" : "");
        n = readdir(fd, &d);
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return list(".");

    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (argc > 2) printf("%s:\n", argv[i]);
        status |= list(argv[i]);
    }
    return status;
}
