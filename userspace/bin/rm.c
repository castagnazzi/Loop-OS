/* rm: remove files and empty directories */

#include <ulib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: rm <file>...");
        return 1;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        long r = unlink(argv[i]);
        if (r < 0) {
            printf("rm: cannot remove '%s' (%s)\n", argv[i], r == -39 ? "directory not empty" : "no such file");
            status = 1;
        }
    }
    return status;
}
