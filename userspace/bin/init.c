/* init: the first user process (pid 1). Starts the shell and waits for it.
 * When the shell exits, init exits and the kernel falls back to its own
 * built-in shell. */

#include <ulib.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("init: pid %d, starting /bin/sh\n", getpid());

    char *sh_argv[] = { "sh", NULL };
    int pid = spawn("/bin/sh", sh_argv);
    if (pid < 0) {
        printf("init: cannot start /bin/sh (error %d)\n", -pid);
        return 1;
    }

    int status = 0;
    waitpid(pid, &status, 0);
    printf("init: shell exited with status %d\n", status);
    return status;
}
