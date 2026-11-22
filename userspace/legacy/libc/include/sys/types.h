#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef int8_t    int8_t;
typedef int16_t   int16_t;
typedef int32_t   int32_t;
typedef int64_t   int64_t;
typedef uint8_t   uint8_t;
typedef uint16_t  uint16_t;
typedef uint32_t  uint32_t;
typedef uint64_t  uint64_t;

typedef int32_t   pid_t;
typedef uint32_t  uid_t;
typedef uint32_t  gid_t;
typedef uint32_t  mode_t;
typedef int64_t   off_t;
typedef int64_t   loff_t;
typedef uint32_t  ino_t;
typedef uint32_t  dev_t;
typedef uint32_t  nlink_t;
typedef int32_t   blksize_t;
typedef int64_t   blkcnt_t;
typedef uint32_t  useconds_t;
typedef int32_t   suseconds_t;

typedef struct {
    int32_t __val[2];
} fsid_t;

#endif