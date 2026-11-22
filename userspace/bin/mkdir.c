/* mkdir: create directories */

#include <ulib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: mkdir <dir>...");
        return 1;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (mkdir(argv[i]) < 0) {
            printf("mkdir: cannot create '%s'\n", argv[i]);
            status = 1;
        }
    }
    return status;
}
