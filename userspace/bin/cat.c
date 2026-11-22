/* cat: print files */

#include <ulib.h>

int main(int argc, char **argv) {
    if (argc < 2) {                       /* no files: copy stdin (pipes) */
        char buf[256];
        ssize_t n;
        while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) write(STDOUT_FILENO, buf, (size_t)n);
        return 0;
    }

    int status = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("cat: %s: no such file\n", argv[i]);
            status = 1;
            continue;
        }
        char buf[256];
        ssize_t n;
        char last = '\n';
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            write(STDOUT_FILENO, buf, (size_t)n);
            last = buf[n - 1];
        }
        if (n < 0) {
            printf("cat: %s: read error %ld\n", argv[i], -n);
            status = 1;
        }
        if (last != '\n') putchar('\n');
        close(fd);
    }
    return status;
}
