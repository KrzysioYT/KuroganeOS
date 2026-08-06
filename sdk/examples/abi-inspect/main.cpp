#include <kurogane/abi.h>

// Compile-only external SDK example. Application startup and the system-call
// transport intentionally remain unavailable until KuroganeOS gains ring 3.
ku_status_t inspect_abi(const ku_abi_descriptor* descriptor) {
    return ku_abi_validate_descriptor(descriptor);
}
