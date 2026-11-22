/* Program entry point.
 *
 * The kernel starts a program with RSP pointing at argc, followed by the
 * argv pointers, a NULL, and the (empty) environment. Hand that address to
 * _start_c(), which calls main() and exits with its return value. */

.section .text
.global _start
.type _start, @function
_start:
    xorl %ebp, %ebp          /* end of the frame chain */
    movq %rsp, %rdi          /* initial stack: argc, argv... */
    andq $-16, %rsp          /* ABI stack alignment */
    call _start_c
    ud2                      /* _start_c never returns */
.size _start, . - _start
