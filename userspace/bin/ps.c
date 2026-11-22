/* ps: list tasks */

#include <ulib.h>

int main(void) {
    static const char *states[] = { "?", "ready", "running", "blocked", "zombie" };
    puts("  PID  PPID  STATE    NAME");

    struct sys_taskinfo ti;
    for (int i = 0; i < 64; i++) {
        if (taskinfo(i, &ti) != 0) continue;
        printf("  %3d  %4d  %-8s %s\n", ti.pid, ti.ppid,
               states[ti.state >= 0 && ti.state <= 4 ? ti.state : 0], ti.name);
    }
    return 0;
}
