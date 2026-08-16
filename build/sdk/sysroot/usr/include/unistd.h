#ifndef KUROGANE_LIBC_UNISTD_H
#define KUROGANE_LIBC_UNISTD_H

#include <stddef.h>
#include <stdint.h>

typedef int64_t ssize_t;
typedef int64_t kuro_fd_t;

#ifdef __cplusplus
extern "C" {
#endif

ssize_t read(kuro_fd_t descriptor, void* buffer, size_t size);
ssize_t write(kuro_fd_t descriptor, const void* buffer, size_t size);
int close(kuro_fd_t descriptor);

#ifdef __cplusplus
}
#endif
#endif
