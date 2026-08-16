#ifndef KUROGANE_LIBC_FCNTL_H
#define KUROGANE_LIBC_FCNTL_H

#include <unistd.h>

#define O_RDONLY 0

#ifdef __cplusplus
extern "C" {
#endif

/* KuroganeOS 1.x open is read-only and has no mode argument. */
kuro_fd_t open(const char* path, int flags);

#ifdef __cplusplus
}
#endif
#endif
