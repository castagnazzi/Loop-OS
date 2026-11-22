/* spin: busy loop forever, to watch preemption. Run it with '&' and the
 * shell stays responsive; stop it with kill. */

#include <ulib.h>

int main(void) {
    volatile unsigned long counter = 0;
    for (;;) counter++;
}
