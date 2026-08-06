#include "../kernel/fs/ramfs.hpp"
#include "../kernel/memory/allocator.hpp"

namespace {

bool text_equals(const char* left, const char* right) {
    if (!left || !right) {
        return left == right;
    }
    while (*left != '\0' && *right != '\0') {
        if (*left != *right) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == *right;
}

struct ListingState {
    size_t count;
    bool saw_log;
    bool saw_state;
};

bool collect_var_entries(const char* name, const fs::FileStat* info, void* context) {
    if (!name || !info || !context) {
        return false;
    }

    ListingState* state = static_cast<ListingState*>(context);
    ++state->count;
    if (text_equals(name, "log") && info->type == fs::EntryType::Directory) {
        state->saw_log = true;
    }
    if (text_equals(name, "state") && info->type == fs::EntryType::File) {
        state->saw_state = true;
    }
    return true;
}

bool stop_after_first(const char*, const fs::FileStat*, void* context) {
    size_t* calls = static_cast<size_t*>(context);
    ++(*calls);
    return false;
}

} // namespace

int main() {
    // Match the production kernel heap so capacity regressions are exercised
    // under the same global memory ceiling.
    alignas(16) static unsigned char heap_storage[2 * 1024 * 1024] = {};
    memory::init_kernel_heap(heap_storage, sizeof(heap_storage));

    if (fs::initialize_ramfs() != fs::Status::Ok) {
        return 1;
    }
    if (fs::initialize_ramfs() != fs::Status::Ok) {
        return 2;
    }

    fs::FileStat info = {};
    if (fs::stat_path("/", &info) != fs::Status::Ok ||
        info.type != fs::EntryType::Directory ||
        info.child_count != 0) {
        return 3;
    }

    if (fs::create_directory_at("/etc") != fs::Status::Ok ||
        fs::create_directory_at("/etc/kurogane") != fs::Status::Ok) {
        return 4;
    }
    if (fs::create_directory_at("/etc") != fs::Status::AlreadyExists) {
        return 5;
    }

    static const char config[] = "enabled=true";
    if (fs::write_file_data(
            "/etc//./kurogane/config",
            config,
            sizeof(config) - 1,
            true
        ) != fs::Status::Ok) {
        return 6;
    }

    char read_buffer[32] = {};
    size_t bytes_read = 0;
    if (fs::read_file_data(
            "etc/kurogane/config",
            read_buffer,
            sizeof(read_buffer),
            &bytes_read
        ) != fs::Status::Ok ||
        bytes_read != sizeof(config) - 1) {
        return 7;
    }
    for (size_t i = 0; i < bytes_read; ++i) {
        if (read_buffer[i] != config[i]) {
            return 8;
        }
    }

    size_t required = 0;
    if (fs::read_file_data(
            "/etc/kurogane/config",
            nullptr,
            0,
            &required
        ) != fs::Status::BufferTooSmall ||
        required != sizeof(config) - 1) {
        return 9;
    }

    fs::write_file("/etc/kurogane/config", "on");
    const char* shortened = fs::read_file("/etc/kurogane/config");
    if (fs::last_status() != fs::Status::Ok ||
        !shortened ||
        shortened[0] != 'o' ||
        shortened[1] != 'n' ||
        shortened[2] != '\0') {
        return 10;
    }

    if (fs::stat_path("/etc/kurogane/config", &info) != fs::Status::Ok ||
        info.type != fs::EntryType::File ||
        info.size != 2) {
        return 11;
    }
    if (fs::create_directory_at("/etc/kurogane/config/child") !=
        fs::Status::NotDirectory) {
        return 12;
    }
    if (fs::write_file_data("/etc/kurogane", "x", 1, false) !=
        fs::Status::IsDirectory) {
        return 13;
    }

    if (fs::create_directory_at("/var") != fs::Status::Ok ||
        fs::create_directory_at("/var/log/") != fs::Status::Ok ||
        fs::write_file_data("var/log/boot", "ready", 5) != fs::Status::Ok ||
        fs::write_file_data("/var/log/../state", "up", 2) != fs::Status::Ok) {
        return 14;
    }

    ListingState listing = {};
    if (fs::list_directory("/var", collect_var_entries, &listing) != fs::Status::Ok ||
        listing.count != 2 ||
        !listing.saw_log ||
        !listing.saw_state) {
        return 15;
    }

    size_t callback_calls = 0;
    if (fs::list_directory("/var", stop_after_first, &callback_calls) !=
            fs::Status::IterationStopped ||
        callback_calls != 1) {
        return 16;
    }
    if (fs::list_directory("/var/state", collect_var_entries, &listing) !=
        fs::Status::NotDirectory) {
        return 17;
    }
    if (fs::list_directory("/", nullptr, nullptr) != fs::Status::InvalidArgument) {
        return 18;
    }

    if (fs::remove_path("/var", false) != fs::Status::DirectoryNotEmpty ||
        fs::remove_path("/var", true) != fs::Status::Ok ||
        fs::stat_path("/var", &info) != fs::Status::NotFound) {
        return 19;
    }
    if (fs::remove_path("/", true) != fs::Status::RootProtected) {
        return 20;
    }

    const unsigned char binary[] = {0x00, 0x11, 0x7f, 0xff};
    if (fs::write_file_data("/blob", binary, sizeof(binary)) != fs::Status::Ok) {
        return 21;
    }
    unsigned char binary_copy[sizeof(binary)] = {};
    if (fs::read_file_data(
            "/blob",
            binary_copy,
            sizeof(binary_copy),
            &bytes_read
        ) != fs::Status::Ok ||
        bytes_read != sizeof(binary)) {
        return 22;
    }
    for (size_t i = 0; i < sizeof(binary); ++i) {
        if (binary_copy[i] != binary[i]) {
            return 23;
        }
    }

    if (fs::write_file_data("/empty", nullptr, 0) != fs::Status::Ok ||
        fs::read_file_data("/empty", nullptr, 0, &bytes_read) != fs::Status::Ok ||
        bytes_read != 0) {
        return 24;
    }

    if (fs::create_file_at("../escape") != fs::Status::InvalidPath ||
        fs::create_file_at("/invalid-file/") != fs::Status::InvalidPath) {
        return 25;
    }

    char long_name[fs::RAMFS_MAX_NAME_LENGTH + 3] = {};
    long_name[0] = '/';
    for (size_t i = 1; i <= fs::RAMFS_MAX_NAME_LENGTH + 1; ++i) {
        long_name[i] = 'a';
    }
    long_name[fs::RAMFS_MAX_NAME_LENGTH + 2] = '\0';
    if (fs::create_file_at(long_name) != fs::Status::NameTooLong) {
        return 26;
    }

    char long_path[fs::RAMFS_MAX_PATH_LENGTH + 2] = {};
    for (size_t i = 0; i <= fs::RAMFS_MAX_PATH_LENGTH; ++i) {
        long_path[i] = 'a';
    }
    long_path[fs::RAMFS_MAX_PATH_LENGTH + 1] = '\0';
    if (fs::create_file_at(long_path) != fs::Status::PathTooLong) {
        return 27;
    }

    const char one_byte = 'x';
    if (fs::write_file_data(
            "/too-large",
            &one_byte,
            fs::RAMFS_MAX_FILE_SIZE + 1
        ) != fs::Status::FileTooLarge) {
        return 28;
    }

    if (fs::create_directory_at("/limit") != fs::Status::Ok) {
        return 29;
    }
    char child_path[] = "/limit/n00";
    for (size_t i = 0; i < fs::RAMFS_MAX_CHILDREN; ++i) {
        child_path[8] = static_cast<char>('0' + ((i / 10) % 10));
        child_path[9] = static_cast<char>('0' + (i % 10));
        if (fs::create_file_at(child_path) != fs::Status::Ok) {
            return 30;
        }
    }
    if (fs::create_file_at("/limit/overflow") != fs::Status::TooManyChildren) {
        return 31;
    }
    if (fs::remove_path("/limit", true) != fs::Status::Ok) {
        return 32;
    }

    fs::FileEntry* legacy = fs::create_file("legacy");
    if (!legacy || fs::last_status() != fs::Status::Ok) {
        return 33;
    }
    fs::write_file("legacy", "compatible");
    if (!text_equals(fs::read_file("legacy"), "compatible")) {
        return 34;
    }
    if (fs::create_file("legacy") != nullptr ||
        fs::last_status() != fs::Status::AlreadyExists) {
        return 35;
    }

    if (!text_equals(fs::status_message(fs::Status::OutOfMemory), "out of memory")) {
        return 36;
    }

    // A large capacity must not remain pinned after a substantial shrink. The
    // replacement is allocated before the old contents are released, so a
    // failed allocation would leave the original file untouched.
    static unsigned char large_file[60 * 1024] = {};
    for (size_t i = 0; i < sizeof(large_file); ++i) {
        large_file[i] = static_cast<unsigned char>(i & 0xffu);
    }
    fs::FileEntry* shrink_entry = nullptr;
    if (fs::create_file_at("/shrink", &shrink_entry) != fs::Status::Ok ||
        !shrink_entry ||
        fs::write_file_data(
            "/shrink", large_file, sizeof(large_file), false) !=
            fs::Status::Ok ||
        shrink_entry->capacity != sizeof(large_file) + 1) {
        return 37;
    }
    const size_t heap_with_large_file = memory::used_bytes();

    // Force the shrink allocation to fail once. The old size, capacity and
    // bytes must remain intact until a replacement buffer can be committed.
    void* pressure[512] = {};
    size_t pressure_count = 0;
    while (pressure_count < 512) {
        void* block = memory::kmalloc(4096);
        if (!block) {
            break;
        }
        pressure[pressure_count++] = block;
    }
    if (pressure_count == 512) {
        return 66;
    }
    static unsigned char failed_reduction[8 * 1024] = {};
    if (fs::write_file_data(
            "/shrink",
            failed_reduction,
            sizeof(failed_reduction),
            false) != fs::Status::OutOfMemory ||
        shrink_entry->size != sizeof(large_file) ||
        shrink_entry->capacity != sizeof(large_file) + 1 ||
        static_cast<unsigned char>(shrink_entry->content[0]) != large_file[0] ||
        static_cast<unsigned char>(
            shrink_entry->content[sizeof(large_file) - 1]) !=
            large_file[sizeof(large_file) - 1]) {
        return 67;
    }
    for (size_t i = 0; i < pressure_count; ++i) {
        memory::kfree(pressure[i]);
    }
    if (memory::used_bytes() != heap_with_large_file) {
        return 68;
    }

    static const char reduced[] = "small";
    if (fs::write_file_data(
            "/shrink", reduced, sizeof(reduced) - 1, false) !=
            fs::Status::Ok ||
        shrink_entry->capacity != sizeof(reduced) ||
        shrink_entry->size != sizeof(reduced) - 1 ||
        memory::used_bytes() + 50 * 1024 >= heap_with_large_file) {
        return 38;
    }
    char reduced_copy[sizeof(reduced)] = {};
    if (fs::read_file_data(
            "/shrink",
            reduced_copy,
            sizeof(reduced_copy),
            &bytes_read) != fs::Status::Ok ||
        bytes_read != sizeof(reduced) - 1 ||
        !text_equals(reduced_copy, reduced)) {
        return 39;
    }
    if (fs::remove_path("/shrink") != fs::Status::Ok) {
        return 40;
    }

    // File copies are complete and independent before the destination becomes
    // visible.
    if (fs::create_directory_at("/ops") != fs::Status::Ok ||
        fs::write_file_data("/ops/source", "original", 8) != fs::Status::Ok ||
        fs::copy_file("/ops/source", "/ops/copy") != fs::Status::Ok) {
        return 41;
    }
    char operation_buffer[32] = {};
    if (fs::read_file_data(
            "/ops/copy",
            operation_buffer,
            sizeof(operation_buffer),
            &bytes_read) != fs::Status::Ok ||
        bytes_read != 8 ||
        !text_equals(operation_buffer, "original")) {
        return 42;
    }
    if (fs::write_file_data("/ops/source", "changed", 7, false) !=
            fs::Status::Ok ||
        fs::read_file_data(
            "/ops/copy",
            operation_buffer,
            sizeof(operation_buffer),
            &bytes_read) != fs::Status::Ok ||
        bytes_read != 8 ||
        !text_equals(operation_buffer, "original")) {
        return 43;
    }

    const size_t before_failed_copies = memory::used_bytes();
    if (fs::copy_file("/ops", "/directory-copy") !=
            fs::Status::IsDirectory ||
        fs::copy_file("/missing", "/missing-copy") !=
            fs::Status::NotFound ||
        fs::copy_file("/ops/source", "/ops/copy") !=
            fs::Status::AlreadyExists ||
        fs::copy_file("/ops/source", "/missing-parent/copy") !=
            fs::Status::NotFound ||
        fs::copy_file("/ops/source", "/ops/trailing/") !=
            fs::Status::InvalidPath ||
        memory::used_bytes() != before_failed_copies ||
        fs::stat_path("/directory-copy", &info) != fs::Status::NotFound ||
        fs::stat_path("/missing-copy", &info) != fs::Status::NotFound ||
        fs::stat_path("/ops/trailing", &info) != fs::Status::NotFound) {
        return 44;
    }

    // Rename and cross-directory moves preserve identity and contents.
    if (fs::rename_path("/ops/copy", "/ops/renamed") != fs::Status::Ok ||
        fs::stat_path("/ops/copy", &info) != fs::Status::NotFound ||
        fs::create_directory_at("/ops/target") != fs::Status::Ok ||
        fs::move_path("/ops/renamed", "/ops/target/moved") !=
            fs::Status::Ok ||
        fs::stat_path("/ops/renamed", &info) != fs::Status::NotFound ||
        fs::read_file_data(
            "/ops/target/moved",
            operation_buffer,
            sizeof(operation_buffer),
            &bytes_read) != fs::Status::Ok ||
        bytes_read != 8 ||
        !text_equals(operation_buffer, "original")) {
        return 45;
    }
    for (size_t i = 0; i < sizeof(operation_buffer); ++i) {
        operation_buffer[i] = '\0';
    }
    if (fs::create_directory_at("/ops/tree") != fs::Status::Ok ||
        fs::create_directory_at("/ops/tree/child") != fs::Status::Ok ||
        fs::write_file_data("/ops/tree/child/data", "tree", 4) !=
            fs::Status::Ok ||
        fs::move_path("/ops/tree", "/moved-tree") != fs::Status::Ok ||
        fs::stat_path("/ops/tree", &info) != fs::Status::NotFound ||
        fs::read_file_data(
            "/moved-tree/child/data",
            operation_buffer,
            sizeof(operation_buffer),
            &bytes_read) != fs::Status::Ok ||
        bytes_read != 4 ||
        !text_equals(operation_buffer, "tree") ||
        fs::move_path("/moved-tree", "/moved-tree") != fs::Status::Ok) {
        return 46;
    }

    // A directory can never be reparented below itself. All failures below
    // must leave the complete source tree reachable at its original path.
    if (fs::move_path(
            "/moved-tree", "/moved-tree/child/loop") !=
            fs::Status::WouldCreateCycle ||
        fs::read_file_data(
            "/moved-tree/child/data",
            operation_buffer,
            sizeof(operation_buffer),
            &bytes_read) != fs::Status::Ok ||
        fs::create_directory_at("/collision") != fs::Status::Ok ||
        fs::move_path("/moved-tree", "/collision") !=
            fs::Status::AlreadyExists ||
        fs::stat_path("/moved-tree", &info) != fs::Status::Ok ||
        info.type != fs::EntryType::Directory) {
        return 47;
    }

    // Destination capacity is validated before detaching the source.
    if (fs::create_directory_at("/full") != fs::Status::Ok) {
        return 48;
    }
    char full_child[] = "/full/n00";
    for (size_t i = 0; i < fs::RAMFS_MAX_CHILDREN; ++i) {
        full_child[7] = static_cast<char>('0' + ((i / 10) % 10));
        full_child[8] = static_cast<char>('0' + (i % 10));
        if (fs::create_file_at(full_child) != fs::Status::Ok) {
            return 49;
        }
    }
    for (size_t i = 0; i < sizeof(operation_buffer); ++i) {
        operation_buffer[i] = '\0';
    }
    if (fs::move_path("/ops/source", "/full/moved") !=
            fs::Status::TooManyChildren ||
        fs::copy_file("/ops/source", "/full/copied") !=
            fs::Status::TooManyChildren ||
        fs::read_file_data(
            "/ops/source",
            operation_buffer,
            sizeof(operation_buffer),
            &bytes_read) != fs::Status::Ok ||
        bytes_read != 7 ||
        !text_equals(operation_buffer, "changed") ||
        fs::stat_path("/full/moved", &info) != fs::Status::NotFound ||
        fs::stat_path("/full/copied", &info) != fs::Status::NotFound) {
        return 50;
    }
    if (fs::remove_path("/full", true) != fs::Status::Ok) {
        return 51;
    }

    // Moving a subtree must account for the depth of every descendant, not
    // just the destination root.
    char deep_parent[fs::RAMFS_MAX_PATH_LENGTH + 1] = "/";
    size_t deep_length = 1;
    for (size_t i = 0; i < fs::RAMFS_MAX_PATH_DEPTH - 1; ++i) {
        if (deep_length > 1) {
            deep_parent[deep_length++] = '/';
        }
        deep_parent[deep_length++] = 'd';
        deep_parent[deep_length] = '\0';
        if (fs::create_directory_at(deep_parent) != fs::Status::Ok) {
            return 52;
        }
    }
    if (fs::create_directory_at("/depth-source") != fs::Status::Ok ||
        fs::create_directory_at("/depth-source/child") != fs::Status::Ok) {
        return 53;
    }
    char deep_destination[fs::RAMFS_MAX_PATH_LENGTH + 1] = {};
    size_t destination_length = 0;
    while (deep_parent[destination_length] != '\0') {
        deep_destination[destination_length] = deep_parent[destination_length];
        ++destination_length;
    }
    const char moved_suffix[] = "/moved";
    for (size_t i = 0; i < sizeof(moved_suffix); ++i) {
        deep_destination[destination_length + i] = moved_suffix[i];
    }
    if (fs::move_path("/depth-source", deep_destination) !=
            fs::Status::PathTooDeep ||
        fs::stat_path("/depth-source/child", &info) != fs::Status::Ok) {
        return 54;
    }
    if (fs::remove_path("/d", true) != fs::Status::Ok ||
        fs::remove_path("/depth-source", true) != fs::Status::Ok) {
        return 55;
    }

    // The same invariant applies to the absolute length of descendant paths.
    char long_parent[fs::RAMFS_MAX_PATH_LENGTH + 1] = "/";
    char long_top[fs::RAMFS_MAX_NAME_LENGTH + 2] = "/";
    size_t long_length = 1;
    for (size_t component = 0; component < 4; ++component) {
        if (long_length > 1) {
            long_parent[long_length++] = '/';
        }
        for (size_t i = 0; i < 60; ++i) {
            const char value = static_cast<char>('a' + component);
            long_parent[long_length++] = value;
            if (component == 0) {
                long_top[i + 1] = value;
            }
        }
        long_parent[long_length] = '\0';
        if (fs::create_directory_at(long_parent) != fs::Status::Ok) {
            return 56;
        }
    }
    long_top[61] = '\0';
    if (fs::create_directory_at("/length-source") != fs::Status::Ok ||
        fs::create_file_at("/length-source/abcdefghij") != fs::Status::Ok) {
        return 57;
    }
    char long_destination[fs::RAMFS_MAX_PATH_LENGTH + 1] = {};
    for (size_t i = 0; i < long_length; ++i) {
        long_destination[i] = long_parent[i];
    }
    long_destination[long_length] = '/';
    long_destination[long_length + 1] = 'm';
    long_destination[long_length + 2] = '\0';
    if (fs::move_path("/length-source", long_destination) !=
            fs::Status::PathTooLong ||
        fs::stat_path("/length-source/abcdefghij", &info) != fs::Status::Ok) {
        return 58;
    }
    if (fs::remove_path(long_top, true) != fs::Status::Ok ||
        fs::remove_path("/length-source", true) != fs::Status::Ok) {
        return 59;
    }

    if (fs::remove_path("/ops", true) != fs::Status::Ok ||
        fs::remove_path("/moved-tree", true) != fs::Status::Ok ||
        fs::remove_path("/collision", true) != fs::Status::Ok) {
        return 60;
    }

    // A copy rejected by the global byte limit must not allocate or publish a
    // destination. Use a large source and fill a dedicated directory until the
    // deterministic storage limit is reached.
    static unsigned char budget_source[32 * 1024] = {};
    for (size_t i = 0; i < sizeof(budget_source); ++i) {
        budget_source[i] = static_cast<unsigned char>((i * 17u) & 0xffu);
    }
    if (fs::create_directory_at("/copy-budget") != fs::Status::Ok ||
        fs::write_file_data(
            "/budget-source",
            budget_source,
            sizeof(budget_source)) != fs::Status::Ok) {
        return 61;
    }
    char budget_path[] = "/copy-budget/c00";
    size_t failed_index = fs::RAMFS_MAX_CHILDREN;
    size_t heap_before_budget_failure = 0;
    for (size_t i = 0; i < fs::RAMFS_MAX_CHILDREN; ++i) {
        budget_path[14] = static_cast<char>('0' + ((i / 10) % 10));
        budget_path[15] = static_cast<char>('0' + (i % 10));
        const size_t heap_before_copy = memory::used_bytes();
        const fs::Status status =
            fs::copy_file("/budget-source", budget_path);
        if (status == fs::Status::StorageLimitReached) {
            failed_index = i;
            heap_before_budget_failure = heap_before_copy;
            break;
        }
        if (status != fs::Status::Ok) {
            return 62;
        }
    }
    if (failed_index == fs::RAMFS_MAX_CHILDREN ||
        memory::used_bytes() != heap_before_budget_failure ||
        fs::stat_path(budget_path, &info) != fs::Status::NotFound ||
        fs::stat_path("/budget-source", &info) != fs::Status::Ok ||
        info.type != fs::EntryType::File ||
        info.size != sizeof(budget_source)) {
        return 63;
    }
    static unsigned char budget_copy[sizeof(budget_source)] = {};
    if (fs::read_file_data(
            "/budget-source",
            budget_copy,
            sizeof(budget_copy),
            &bytes_read) != fs::Status::Ok ||
        bytes_read != sizeof(budget_source) ||
        budget_copy[0] != budget_source[0] ||
        budget_copy[sizeof(budget_copy) - 1] !=
            budget_source[sizeof(budget_source) - 1]) {
        return 64;
    }
    if (fs::remove_path("/copy-budget", true) != fs::Status::Ok ||
        fs::remove_path("/budget-source") != fs::Status::Ok ||
        !text_equals(
            fs::status_message(fs::Status::WouldCreateCycle),
            "move would create a directory cycle")) {
        return 65;
    }

    return 0;
}
