/* cp: copy a file */

#include <ulib.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        puts("usage: cp <source> <destination>");
        return 1;
    }
    int in = open(argv[1], O_RDONLY);
    if (in < 0) {
        printf("cp: %s: no such file\n", argv[1]);
        return 1;
    }
    int out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC);
    if (out < 0) {
        printf("cp: cannot create %s\n", argv[2]);
        return 1;
    }
    char buf[256];
    ssize_t n;
    while ((n = read(in, buf, sizeof(buf))) > 0) write(out, buf, (size_t)n);
    close(in);
    close(out);
    return 0;
}
