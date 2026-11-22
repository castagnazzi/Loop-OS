/* kill: terminate a task by pid */

#include <ulib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: kill <pid>");
        return 1;
    }
    int r = kill(atoi(argv[1]), 9);
    if (r < 0) {
        printf("kill: %s: no such process\n", argv[1]);
        return 1;
    }
    return 0;
}
