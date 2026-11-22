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
            const char *why = r == -39 ? "directory not empty" : r == -1 ? "system file, protected"
                              : r == -30 ? "read-only filesystem" : "no such file";
            printf("rm: cannot remove '%s' (%s)\n", argv[i], why);
            status = 1;
        }
    }
    return status;
}
