#ifndef KUROGANE_SDK_TYPES_H
#define KUROGANE_SDK_TYPES_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#define KUROGANE_EXTERN_C extern "C"
#else
#define KUROGANE_EXTERN_C extern
#endif

#if defined(__GNUC__) || defined(__clang__)
#define KUROGANE_NODISCARD __attribute__((warn_unused_result))
#else
#define KUROGANE_NODISCARD
#endif

typedef uint64_t ku_handle_t;
typedef int32_t ku_status_t;

#define KU_INVALID_HANDLE UINT64_C(0)

typedef struct ku_string_view {
    const char* data;
    size_t size;
} ku_string_view;

typedef struct ku_mutable_buffer {
    void* data;
    size_t size;
} ku_mutable_buffer;

#if defined(__cplusplus)
static_assert(sizeof(ku_handle_t) == 8, "Kurogane handle ABI mismatch");
static_assert(sizeof(ku_string_view) == 16, "Kurogane string ABI mismatch");
static_assert(sizeof(ku_mutable_buffer) == 16, "Kurogane buffer ABI mismatch");
#else
_Static_assert(sizeof(ku_handle_t) == 8, "Kurogane handle ABI mismatch");
_Static_assert(sizeof(ku_string_view) == 16, "Kurogane string ABI mismatch");
_Static_assert(sizeof(ku_mutable_buffer) == 16, "Kurogane buffer ABI mismatch");
#endif

#endif
