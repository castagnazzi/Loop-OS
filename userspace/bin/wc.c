/* wc: count lines, words and bytes of the files given, or of stdin */

#include <ulib.h>

static void count(int fd, const char *name) {
    unsigned long lines = 0, words = 0, bytes = 0;
    int in_word = 0;
    char buf[256];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            bytes++;
            if (c == '\n') lines++;
            if (c == ' ' || c == '\n' || c == '\t') in_word = 0;
            else if (!in_word) { in_word = 1; words++; }
        }
    }
    printf("%lu %lu %lu%s%s\n", lines, words, bytes, name ? " " : "", name ? name : "");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        count(STDIN_FILENO, NULL);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("wc: %s: no such file\n", argv[i]);
            continue;
        }
        count(fd, argv[i]);
        close(fd);
    }
    return 0;
}
