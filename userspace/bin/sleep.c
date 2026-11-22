/* sleep: wait N seconds (the task blocks, other tasks keep running) */

#include <ulib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: sleep <seconds>");
        return 1;
    }
    sleep_ms((unsigned long)atoi(argv[1]) * 1000);
    return 0;
}
