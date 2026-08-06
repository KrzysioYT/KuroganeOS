#include <kurogane/abi.h>

#include <cassert>
#include <cstddef>

int main() {
    static_assert(offsetof(ku_abi_descriptor, available_features) == 16);
    static_assert(offsetof(ku_abi_descriptor, reserved) == 24);

    assert(ku_abi_validate_descriptor(nullptr) ==
           KU_STATUS_INVALID_ARGUMENT);

    ku_abi_descriptor descriptor{};
    descriptor.structure_size = sizeof(descriptor);
    descriptor.abi_version = KU_ABI_VERSION_CURRENT;
    descriptor.architecture = KU_ARCHITECTURE_X86_64;
    descriptor.page_size = 4096;
    descriptor.available_features = KU_ABI_FEATURE_TIME;
    assert(ku_abi_validate_descriptor(&descriptor) == KU_STATUS_OK);

    descriptor.abi_version = UINT32_C(2) << 16;
    assert(ku_abi_validate_descriptor(&descriptor) ==
           KU_STATUS_VERSION_MISMATCH);
    descriptor.abi_version = KU_ABI_VERSION_CURRENT;

    descriptor.page_size = 3000;
    assert(ku_abi_validate_descriptor(&descriptor) ==
           KU_STATUS_NOT_SUPPORTED);
    descriptor.page_size = 4096;

    descriptor.structure_size = sizeof(descriptor) - 1;
    assert(ku_abi_validate_descriptor(&descriptor) ==
           KU_STATUS_CORRUPT_DATA);
}
