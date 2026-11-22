#include <errno.h>

int errno = 0;

const char *strerror(int errnum) {
    static const char *errors[] = {
        [0] = "Success",
        [1] = "Operation not permitted",
        [2] = "No such file or directory",
        [3] = "No such process",
        [4] = "Interrupted system call",
        [5] = "I/O error",
        [6] = "No such device or address",
        [7] = "Argument list too long",
        [8] = "Exec format error",
        [9] = "Bad file descriptor",
        [10] = "No child processes",
        [11] = "Try again",
        [12] = "Out of memory",
        [13] = "Permission denied",
        [14] = "Bad address",
        [15] = "Block device required",
        [16] = "Device or resource busy",
        [17] = "File exists",
        [18] = "Invalid cross-device link",
        [19] = "No such device",
        [20] = "Not a directory",
        [21] = "Is a directory",
        [22] = "Invalid argument",
        [23] = "File table overflow",
        [24] = "Too many open files",
        [25] = "Not a typewriter",
        [26] = "Text file busy",
        [27] = "File too large",
        [28] = "No space left on device",
        [29] = "Illegal seek",
        [30] = "Read-only file system",
        [31] = "Too many links",
        [32] = "Broken pipe",
        [33] = "Math argument out of domain",
        [34] = "Math result not representable",
        [35] = "Resource deadlock would occur",
        [36] = "File name too long",
        [37] = "No locks available",
        [38] = "Function not implemented",
        [39] = "Directory not empty",
        [40] = "Too many levels of symbolic links",
        [41] = "Unknown error",
    };
    
    if (errnum >= 0 && errnum < (int)(sizeof(errors)/sizeof(errors[0])) && errors[errnum]) {
        return errors[errnum];
    }
    return "Unknown error";
}

void perror(const char *s) {
    if (s && *s) {
        write(STDERR_FILENO, s, strlen(s));
        write(STDERR_FILENO, ": ", 2);
    }
    write(STDERR_FILENO, strerror(errno), strlen(strerror(errno)));
    write(STDERR_FILENO, "\n", 1);
}