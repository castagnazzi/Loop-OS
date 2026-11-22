/* Userspace C Runtime Startup */
/* Minimal crt0 for Loop-OS userspace */

#include <stddef.h>

/* External symbols from linker script */
extern char __heap_start[];
extern char __stack_top[];

/* Main function */
extern int main(int argc, char *argv[], char *envp[]);

/* Environment */
extern char **environ;

/* Startup function */
void _start(void) {
    /* Stack is already set up by kernel */
    /* Heap is set up by linker script */
    
    /* Get argc, argv, envp from stack */
    /* Kernel puts them on stack: argc, argv[], envp[] */
    register long rsp asm("rsp");
    long *stack = (long *)rsp;
    
    int argc = stack[0];
    char **argv = (char **)&stack[1];
    char **envp = &argv[argc + 1];
    
    /* Set up environ */
    environ = envp;
    
    /* Call constructors if any */
    /* TODO: .init_array */
    
    /* Call main */
    int ret = main(argc, argv, envp);
    
    /* Exit */
    _exit(ret);
}

/* Exit function */
void _exit(int status) {
    asm volatile("mov $60, %%rax; syscall" : : "D"(status) : "rax", "rcx", "r11", "memory");
    __builtin_unreachable();
}

/* Dummy functions for C++ */
void __cxa_pure_virtual(void) { _exit(1); }