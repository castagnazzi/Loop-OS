#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syscall.h>

/* Mount virtual filesystems */
static void mount_vfs(void) {
    mkdir("/proc", 0555);
    mkdir("/sys", 0555);
    mkdir("/dev", 0755);
    mkdir("/tmp", 0777);
    mkdir("/home", 0755);
    mkdir("/root", 0700);
    
    /* Create basic device nodes */
    mknod("/dev/null", S_IFCHR | 0666, (1 << 8) | 3);
    mknod("/dev/zero", S_IFCHR | 0666, (1 << 8) | 5);
    mknod("/dev/tty", S_IFCHR | 0666, (5 << 8) | 0);
    mknod("/dev/console", S_IFCHR | 0666, (5 << 8) | 1);
    mknod("/dev/tty0", S_IFCHR | 0666, (4 << 8) | 0);
    mknod("/dev/fb0", S_IFCHR | 0666, (29 << 8) | 0);
}

/* Launch shell on console */
static void launch_shell(void) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }
    
    if (pid == 0) {
        /* Child - exec shell */
        setsid();
        ioctl(STDIN_FILENO, 0x5401, 0); /* TIOCSCTTY */
        
        /* Redirect stdio to console */
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        open("/dev/console", O_RDWR);
        dup(0);
        dup(0);
        
        /* Exec shell */
        execl("/bin/sh", "sh", NULL);
        perror("execl /bin/sh");
        _exit(1);
    }
    
    /* Parent - wait for shell */
    int status;
    waitpid(pid, &status, 0);
    printf("Shell exited with status %d, restarting...\n", status);
}

/* Reap zombie children */
static void reap_children(void) {
    while (1) {
        pid_t pid = waitpid(-1, NULL, WNOHANG);
        if (pid <= 0) break;
    }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    
    printf("=== Loop-OS Init ===\n");
    
    /* Mount virtual filesystems */
    printf("Mounting virtual filesystems...\n");
    mount_vfs();
    
    /* Set up environment */
    setenv("PATH", "/bin:/usr/bin", 1);
    setenv("HOME", "/root", 1);
    setenv("TERM", "linux", 1);
    setenv("USER", "root", 1);
    setenv("SHELL", "/bin/sh", 1);
    
    printf("Starting shell on /dev/console...\n");
    
    /* Main loop - restart shell on exit */
    while (1) {
        launch_shell();
        reap_children();
        sleep(1);
    }
    
    return 0;
}