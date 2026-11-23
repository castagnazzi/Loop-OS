/* teclado: show or change the keyboard layout.  usage: teclado [br|us] */

#include <ulib.h>

int main(int argc, char **argv) {
    if (argc > 1) {
        if (keymap(argv[1]) < 0) {
            puts("uso: teclado [br|us]");
            return 1;
        }
    }
    printf("layout: %s\n", keymap(0) ? "ABNT2 (br)" : "US (us)");
    return 0;
}
