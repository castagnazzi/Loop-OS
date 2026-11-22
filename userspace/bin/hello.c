/* hello: first program to run in ring 3. Shows argv, the pid, sbrk-backed
 * memory and the uptime, so one run exercises several system calls. */

#include <ulib.h>

int main(int argc, char **argv) {
    printf("Hello from userspace! I am pid %d.\n", getpid());
    for (int i = 0; i < argc; i++) printf("  argv[%d] = \"%s\"\n", i, argv[i]);

    /* Heap: grow the break by 64 KiB, fill it and check it back */
    size_t size = 64 * 1024;
    unsigned char *mem = sbrk((long)size);
    if (mem == (void *)-1) {
        puts("sbrk failed");
        return 1;
    }
    for (size_t i = 0; i < size; i++) mem[i] = (unsigned char)(i * 7);
    int ok = 1;
    for (size_t i = 0; i < size; i++) {
        if (mem[i] != (unsigned char)(i * 7)) { ok = 0; break; }
    }
    printf("sbrk: %lu bytes at %p, contents %s\n", (unsigned long)size, (void *)mem,
           ok ? "verified" : "CORRUPTED");

    unsigned long ms = uptime_ms();
    printf("uptime: %lu.%03lu s\n", ms / 1000, ms % 1000);
    return ok ? 0 : 1;
}
