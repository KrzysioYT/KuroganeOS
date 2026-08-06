#ifndef KUROGANE_SDK_ABI_H
#define KUROGANE_SDK_ABI_H

#include <kurogane/status.h>

#define KU_ABI_VERSION_MAJOR UINT16_C(1)
#define KU_ABI_VERSION_MINOR UINT16_C(0)
#define KU_ABI_VERSION_CURRENT \
    ((uint32_t)(KU_ABI_VERSION_MAJOR << 16) | KU_ABI_VERSION_MINOR)

enum ku_architecture {
    KU_ARCHITECTURE_UNKNOWN = 0,
    KU_ARCHITECTURE_X86_64 = 1
};

enum ku_abi_feature {
    KU_ABI_FEATURE_PROCESSES = UINT64_C(1) << 0,
    KU_ABI_FEATURE_THREADS = UINT64_C(1) << 1,
    KU_ABI_FEATURE_VIRTUAL_MEMORY = UINT64_C(1) << 2,
    KU_ABI_FEATURE_FILES = UINT64_C(1) << 3,
    KU_ABI_FEATURE_NETWORK = UINT64_C(1) << 4,
    KU_ABI_FEATURE_GUI = UINT64_C(1) << 5,
    KU_ABI_FEATURE_AUDIO = UINT64_C(1) << 6,
    KU_ABI_FEATURE_TIME = UINT64_C(1) << 7,
    KU_ABI_FEATURE_INPUT = UINT64_C(1) << 8,
    KU_ABI_FEATURE_IPC = UINT64_C(1) << 9,
    KU_ABI_FEATURE_PACKAGES = UINT64_C(1) << 10,
    KU_ABI_FEATURE_PERMISSIONS = UINT64_C(1) << 11
};

typedef struct ku_abi_descriptor {
    uint32_t structure_size;
    uint32_t abi_version;
    uint32_t architecture;
    uint32_t page_size;
    uint64_t available_features;
    uint64_t reserved[3];
} ku_abi_descriptor;

static inline ku_status_t ku_abi_validate_descriptor(
    const ku_abi_descriptor* descriptor) {
    if (descriptor == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    if (descriptor->structure_size < sizeof(ku_abi_descriptor)) {
        return KU_STATUS_CORRUPT_DATA;
    }
    if ((descriptor->abi_version >> 16) != KU_ABI_VERSION_MAJOR) {
        return KU_STATUS_VERSION_MISMATCH;
    }
    if (descriptor->architecture != KU_ARCHITECTURE_X86_64 ||
        descriptor->page_size == 0 ||
        (descriptor->page_size & (descriptor->page_size - 1)) != 0) {
        return KU_STATUS_NOT_SUPPORTED;
    }
    return KU_STATUS_OK;
}

#if defined(__cplusplus)
static_assert(sizeof(ku_abi_descriptor) == 48, "ABI descriptor size mismatch");
static_assert(alignof(ku_abi_descriptor) == 8, "ABI descriptor alignment mismatch");
#else
_Static_assert(sizeof(ku_abi_descriptor) == 48, "ABI descriptor size mismatch");
_Static_assert(_Alignof(ku_abi_descriptor) == 8, "ABI descriptor alignment mismatch");
#endif

#endif
