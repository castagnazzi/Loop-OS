#ifndef _UNISTD_H
#define _UNISTD_H

#include <sys/types.h>

/* Standard file descriptors */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* access() flags */
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

/* whence for lseek */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Process */
pid_t fork(void);
pid_t vfork(void);
pid_t getpid(void);
pid_t getppid(void);
int execve(const char *path, char *const argv[], char *const envp[]);
int execv(const char *path, char *const argv[]);
int execl(const char *path, const char *arg, ...);
int execvp(const char *file, char *const argv[]);
int execvpe(const char *file, char *const argv[], char *const envp[]);
void _exit(int status);

/* File operations */
int open(const char *pathname, int flags, ...);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int stat(const char *pathname, struct stat *statbuf);
int fstat(int fd, struct stat *statbuf);
int mkdir(const char *pathname, mode_t mode);
int rmdir(const char *pathname);
int unlink(const char *pathname);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);

/* Directory */
struct dirent {
    ino_t d_ino;
    off_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];
};

typedef struct {
    int fd;
    struct dirent entry;
} DIR;

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);

/* Environment */
extern char **environ;
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);

/* Sleep */
unsigned int sleep(unsigned int seconds);
int usleep(useconds_t usec);

/* System */
int system(const char *command);

/* Hostname */
int gethostname(char *name, size_t len);
int sethostname(const char *name, size_t len);

#endif