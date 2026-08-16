#include <kurogane/abi.h>
#include <kurogane/syscall.h>
#include <kurogane/ui.h>

#include <cassert>
#include <cstddef>

int main() {
    static_assert(KU_SYS_EXIT == 1);
    static_assert(KU_SYS_WRITE == 2);
    static_assert(KU_SYS_GETPID == 3);
    static_assert(KU_SYS_READ == 4);
    static_assert(KU_SYS_OPEN == 5);
    static_assert(KU_SYS_CLOSE == 6);
    static_assert(KU_SYS_ALLOC == 7);
    static_assert(KU_SYS_FREE == 8);
    static_assert(KU_SYS_SLEEP == 9);
    static_assert(KU_SYS_YIELD == 10);
    static_assert(KU_SYS_GETTID == 11);
    static_assert(KU_SYS_SPAWN == 12);
    static_assert(KU_SYS_WAIT == 13);
    static_assert(KU_SYS_UI_CREATE == 14);
    static_assert(KU_SYS_UI_PRESENT == 15);
    static_assert(KU_SYS_UI_POLL == 16);
    static_assert(KU_SYS_UI_CLOSE == 17);
    static_assert(sizeof(ku_ui_window_options) == 20);
    static_assert(sizeof(ku_ui_frame) == 800);
    static_assert(sizeof(ku_ui_event) == 32);
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
