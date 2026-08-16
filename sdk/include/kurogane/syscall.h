#ifndef KUROGANE_SDK_SYSCALL_H
#define KUROGANE_SDK_SYSCALL_H

#include <kurogane/status.h>

/*
 * Stable syscall numbers. The x86-64 v1 transport is an interrupt gate at
 * vector 0x80; applications should use these wrappers instead of spelling the
 * transport inline so a later SYSCALL migration does not change source code.
 * New public services are append-only.
 */
enum ku_syscall_number {
    KU_SYS_INVALID = 0,
    KU_SYS_EXIT = 1,
    KU_SYS_WRITE = 2,
    KU_SYS_GETPID = 3,
    KU_SYS_READ = 4,
    KU_SYS_OPEN = 5,
    KU_SYS_CLOSE = 6,
    KU_SYS_ALLOC = 7,
    KU_SYS_FREE = 8,
    KU_SYS_SLEEP = 9,
    KU_SYS_YIELD = 10,
    KU_SYS_GETTID = 11,
    KU_SYS_SPAWN = 12,
    KU_SYS_WAIT = 13,
    KU_SYS_UI_CREATE = 14,
    KU_SYS_UI_PRESENT = 15,
    KU_SYS_UI_POLL = 16,
    KU_SYS_UI_CLOSE = 17,
    KU_SYS_SYSTEM_SNAPSHOT = 18,
    KU_SYS_DESKTOP_PIN = 19,
    KU_SYS_NET_STATUS = 20,
    KU_SYS_HTTP_GET = 21
};

enum ku_open_flags {
    KU_OPEN_READ = UINT64_C(1) << 0
};

static inline ku_result_t ku_syscall3(
    uint64_t number,
    uint64_t argument1,
    uint64_t argument2,
    uint64_t argument3) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = argument1;
    register uint64_t rsi __asm__("rsi") = argument2;
    register uint64_t rdx __asm__("rdx") = argument3;
    __asm__ volatile(
        "int $0x80"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi), "d"(rdx)
        : "memory", "cc");
    return (ku_result_t)rax;
}

static inline ku_result_t ku_write(
    uint64_t descriptor,
    const void* buffer,
    size_t size) {
    return ku_syscall3(
        KU_SYS_WRITE,
        descriptor,
        (uint64_t)(uintptr_t)buffer,
        (uint64_t)size);
}

static inline uint64_t ku_getpid(void) {
    return (uint64_t)ku_syscall3(KU_SYS_GETPID, 0, 0, 0);
}

static inline uint64_t ku_gettid(void) {
    return (uint64_t)ku_syscall3(KU_SYS_GETTID, 0, 0, 0);
}

static inline ku_result_t ku_read(
    ku_handle_t handle,
    void* buffer,
    size_t size) {
    return ku_syscall3(
        KU_SYS_READ,
        handle,
        (uint64_t)(uintptr_t)buffer,
        (uint64_t)size);
}

static inline ku_result_t ku_open(
    const char* path,
    size_t path_size,
    uint64_t flags) {
    return ku_syscall3(
        KU_SYS_OPEN,
        (uint64_t)(uintptr_t)path,
        (uint64_t)path_size,
        flags);
}

static inline ku_status_t ku_close(ku_handle_t handle) {
    return (ku_status_t)ku_syscall3(KU_SYS_CLOSE, handle, 0, 0);
}

static inline void* ku_alloc(size_t size) {
    return (void*)(uintptr_t)ku_syscall3(KU_SYS_ALLOC, size, 0, 0);
}

static inline ku_status_t ku_free(void* allocation) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_FREE, (uint64_t)(uintptr_t)allocation, 0, 0);
}

static inline ku_status_t ku_sleep(uint64_t timer_ticks) {
    return (ku_status_t)ku_syscall3(KU_SYS_SLEEP, timer_ticks, 0, 0);
}

static inline ku_status_t ku_yield(void) {
    return (ku_status_t)ku_syscall3(KU_SYS_YIELD, 0, 0, 0);
}

static inline ku_result_t ku_spawn(
    const char* path,
    size_t path_size) {
    return ku_syscall3(
        KU_SYS_SPAWN,
        (uint64_t)(uintptr_t)path,
        (uint64_t)path_size,
        0);
}

static inline ku_status_t ku_wait(uint64_t pid, int32_t* exit_code) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_WAIT,
        pid,
        (uint64_t)(uintptr_t)exit_code,
        0);
}

__attribute__((noreturn)) static inline void ku_exit(int32_t status) {
    (void)ku_syscall3(KU_SYS_EXIT, (uint64_t)(int64_t)status, 0, 0);
    for (;;) {
        __asm__ volatile("ud2");
    }
}

#endif
