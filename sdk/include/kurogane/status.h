#ifndef KUROGANE_SDK_STATUS_H
#define KUROGANE_SDK_STATUS_H

#include <kurogane/types.h>

enum ku_status_code {
    KU_STATUS_OK = 0,
    KU_STATUS_INVALID_ARGUMENT = -1,
    KU_STATUS_OUT_OF_RANGE = -2,
    KU_STATUS_NOT_SUPPORTED = -3,
    KU_STATUS_NOT_FOUND = -4,
    KU_STATUS_ALREADY_EXISTS = -5,
    KU_STATUS_ACCESS_DENIED = -6,
    KU_STATUS_OUT_OF_MEMORY = -7,
    KU_STATUS_IO_ERROR = -8,
    KU_STATUS_WOULD_BLOCK = -9,
    KU_STATUS_TIMED_OUT = -10,
    KU_STATUS_INTERRUPTED = -11,
    KU_STATUS_BAD_STATE = -12,
    KU_STATUS_VERSION_MISMATCH = -13,
    KU_STATUS_CORRUPT_DATA = -14,
    KU_STATUS_END_OF_STREAM = -15,
    KU_STATUS_CONNECTION_REFUSED = -16,
    KU_STATUS_CONNECTION_RESET = -17
};

static inline int ku_status_is_success(ku_status_t status) {
    return status >= 0;
}

#endif
