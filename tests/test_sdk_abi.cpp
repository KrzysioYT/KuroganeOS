#include <kurogane/abi.h>
#include <kurogane/audio.h>
#include <kurogane/filesystem.h>
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
    static_assert(KU_SYS_SYSTEM_SNAPSHOT == 18);
    static_assert(KU_SYS_DESKTOP_PIN == 19);
    static_assert(KU_SYS_NET_STATUS == 20);
    static_assert(KU_SYS_HTTP_GET == 21);
    static_assert(KU_SYS_AUDIO_STATUS == 22);
    static_assert(KU_SYS_AUDIO_SET == 23);
    static_assert(KU_SYS_FS_STAT == 24);
    static_assert(KU_SYS_FS_READDIR == 25);
    static_assert(KU_SYS_FS_CREATE == 26);
    static_assert(KU_SYS_FS_UNLINK == 27);
    static_assert(KU_SYS_FS_RENAME == 28);
    static_assert(KU_SYS_FS_MKDIR == 29);
    static_assert(KU_SYS_FS_RMDIR == 30);
    static_assert(KU_SYS_FS_SYNC == 31);
    static_assert(KU_SYS_FS_SEEK == 32);
    static_assert(KU_SYS_AUDIO_PLAY_PCM16 == 33);
    static_assert(KU_SYS_AUDIO_POLL == 34);
    static_assert(KU_SYS_AUDIO_STOP == 35);
    static_assert(KU_SYS_FS_CHDIR == 36);
    static_assert(KU_SYS_FS_GETCWD == 37);

    static_assert(KU_AUDIO_PCM_SAMPLE_RATE == 48000U);
    static_assert(KU_AUDIO_PCM_CHANNELS == 2U);
    static_assert(KU_AUDIO_PCM_BITS_PER_SAMPLE == 16U);
    static_assert(KU_AUDIO_PCM_MAX_FRAMES == 1024U);

    static_assert(KU_OPEN_READ == (UINT64_C(1) << 0));
    static_assert(KU_OPEN_WRITE == (UINT64_C(1) << 1));
    static_assert(KU_OPEN_APPEND == (UINT64_C(1) << 2));
    static_assert(KU_OPEN_DIRECTORY == (UINT64_C(1) << 3));
    static_assert(KU_STATUS_END_OF_STREAM == -15);

    static_assert(KU_FILE_SEEK_BEGIN == 0);
    static_assert(KU_FILE_SEEK_CURRENT == 1);
    static_assert(KU_FILE_SEEK_END == 2);
    static_assert(KU_FILE_NAME_CAPACITY == 64U);
    static_assert(sizeof(ku_file_stat) == 24);
    static_assert(sizeof(ku_directory_entry) == 88);
    static_assert(sizeof(ku_file_rename_request) == 40);
    static_assert(sizeof(ku_file_seek_request) == 40);
    static_assert(offsetof(ku_file_stat, size) == 8);
    static_assert(offsetof(ku_directory_entry, name) == 24);
    static_assert(offsetof(ku_file_rename_request, source) == 8);
    static_assert(offsetof(ku_file_rename_request, destination) == 24);
    static_assert(offsetof(ku_file_seek_request, file) == 8);
    static_assert(offsetof(ku_file_seek_request, offset) == 16);
    static_assert(offsetof(ku_file_seek_request, new_offset) == 24);
    static_assert(offsetof(ku_file_seek_request, reserved) == 32);

    static_assert(sizeof(ku_ui_window_options) == 20);
    static_assert(sizeof(ku_ui_frame) == 800);
    static_assert(sizeof(ku_ui_event) == 32);
    static_assert(offsetof(ku_abi_descriptor, available_features) == 16);
    static_assert(offsetof(ku_abi_descriptor, reserved) == 24);

    assert(ku_abi_validate_descriptor(nullptr) == KU_STATUS_INVALID_ARGUMENT);

    ku_abi_descriptor descriptor{};
    descriptor.structure_size = sizeof(descriptor);
    descriptor.abi_version = KU_ABI_VERSION_CURRENT;
    descriptor.architecture = KU_ARCHITECTURE_X86_64;
    descriptor.page_size = 4096;
    descriptor.available_features = KU_ABI_FEATURE_TIME | KU_ABI_FEATURE_FILES;
    assert(ku_abi_validate_descriptor(&descriptor) == KU_STATUS_OK);

    descriptor.abi_version = UINT32_C(2) << 16;
    assert(ku_abi_validate_descriptor(&descriptor) == KU_STATUS_VERSION_MISMATCH);
    descriptor.abi_version = KU_ABI_VERSION_CURRENT;

    descriptor.page_size = 3000;
    assert(ku_abi_validate_descriptor(&descriptor) == KU_STATUS_NOT_SUPPORTED);
    descriptor.page_size = 4096;

    descriptor.structure_size = sizeof(descriptor) - 1;
    assert(ku_abi_validate_descriptor(&descriptor) == KU_STATUS_CORRUPT_DATA);
}
