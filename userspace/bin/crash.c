/* crash: write to address 0. The kernel should kill only this process. */

#include <ulib.h>

int main(void) {
    puts("crash: about to write to address 0...");
    *(volatile int *)0 = 42;
    puts("crash: not reached");
    return 0;
}
