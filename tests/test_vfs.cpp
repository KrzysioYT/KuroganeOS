#include "../kernel/fs/vfs.hpp"

#include <cstdio>
#include <cstdint>

namespace vfs = fs::vfs;

namespace {

constexpr size_t FAKE_MAX_NODES = 96;
constexpr size_t FAKE_MAX_DATA = 256;

struct FakeNode {
    bool active;
    char path[vfs::MAX_PATH_LENGTH + 1];
    vfs::NodeType type;
    vfs::NodeFlags flags;
    uint8_t data[FAKE_MAX_DATA];
    size_t size;
};

struct FakeFs {
    FakeNode nodes[FAKE_MAX_NODES];
    size_t stat_calls;
    size_t open_calls;
    size_t close_calls;
    size_t read_calls;
    size_t write_calls;
    size_t readdir_calls;
    size_t create_calls;
    size_t unlink_calls;
    size_t rename_calls;
    size_t mkdir_calls;
    size_t rmdir_calls;
    size_t sync_calls;
    char last_path[vfs::MAX_PATH_LENGTH + 1];
    char last_second_path[vfs::MAX_PATH_LENGTH + 1];
    vfs::Status stat_status;
    vfs::Status open_status;
    vfs::Status read_status;
    vfs::Status write_status;
    vfs::Status readdir_status;
    vfs::Status mutation_status;
    vfs::Status sync_status;
    size_t short_io;
    bool overreport_io;
    bool invalid_directory_entry;
};

size_t text_length(const char* text) {
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

bool text_equal(const char* left, const char* right) {
    size_t position = 0;
    while (left[position] != '\0' || right[position] != '\0') {
        if (left[position] != right[position]) {
            return false;
        }
        ++position;
    }
    return true;
}

void copy_text(char* destination, const char* source) {
    size_t position = 0;
    do {
        destination[position] = source[position];
    } while (source[position++] != '\0');
}

void copy_data(void* destination, const void* source, size_t size) {
    uint8_t* output = static_cast<uint8_t*>(destination);
    const uint8_t* input = static_cast<const uint8_t*>(source);
    for (size_t i = 0; i < size; ++i) {
        output[i] = input[i];
    }
}

void reset_fake(FakeFs* fake) {
    *fake = {};
}

FakeNode* add_node(
    FakeFs* fake,
    const char* path,
    vfs::NodeType type,
    const char* content = nullptr) {
    for (size_t i = 0; i < FAKE_MAX_NODES; ++i) {
        FakeNode& node = fake->nodes[i];
        if (node.active) {
            continue;
        }
        node = {};
        node.active = true;
        copy_text(node.path, path);
        node.type = type;
        node.flags = type == vfs::NodeType::Regular
            ? vfs::NodeFlags::Seekable
            : vfs::NodeFlags::None;
        if (content) {
            node.size = text_length(content);
            copy_data(node.data, content, node.size);
        }
        return &node;
    }
    return nullptr;
}

FakeNode* find_node(FakeFs* fake, const char* path) {
    for (size_t i = 0; i < FAKE_MAX_NODES; ++i) {
        if (fake->nodes[i].active &&
            text_equal(fake->nodes[i].path, path)) {
            return &fake->nodes[i];
        }
    }
    return nullptr;
}

size_t node_index(FakeFs* fake, const FakeNode* node) {
    return static_cast<size_t>(node - &fake->nodes[0]);
}

void record_path(FakeFs* fake, const char* path) {
    copy_text(fake->last_path, path);
}

vfs::Status fake_stat(
    void* context,
    const char* path,
    vfs::FileStat* info) {
    FakeFs* fake = static_cast<FakeFs*>(context);
    ++fake->stat_calls;
    record_path(fake, path);
    if (fake->stat_status != vfs::Status::Ok) {
        return fake->stat_status;
    }
    FakeNode* node = find_node(fake, path);
    if (!node) {
        return vfs::Status::NotFound;
    }
    *info = {node->type, node->flags, static_cast<uint64_t>(node->size)};
    return vfs::Status::Ok;
}

vfs::Status fake_open(
    void* context,
    const char* path,
    vfs::OpenFlags,
    vfs::BackendFile* file) {
    FakeFs* fake = static_cast<FakeFs*>(context);
    ++fake->open_calls;
    record_path(fake, path);
    if (fake->open_status != vfs::Status::Ok) {
        return fake->open_status;
    }
    FakeNode* node = find_node(fake, path);
    if (!node) {
        return vfs::Status::NotFound;
    }
    file->words[0] = node_index(fake, node) + 1;
    file->words[1] = 0;
    return vfs::Status::Ok;
}

FakeNode* file_node(FakeFs* fake, const vfs::BackendFile* file) {
    if (file->words[0] == 0 || file->words[0] > FAKE_MAX_NODES) {
        return nullptr;
    }
    FakeNode& node = fake->nodes[file->words[0] - 1];
    return node.active ? &node : nullptr;
}

void fake_close(void* context, const vfs::BackendFile*) {
    FakeFs* fake = static_cast<FakeFs*>(context);
    ++fake->close_calls;
}

vfs::Status fake_read(
    void* context,
    const vfs::BackendFile* file,
    uint64_t offset,
    void* buffer,
    size_t size,
    size_t* bytes_read) {
    FakeFs* fake = static_cast<FakeFs*>(context);
    ++fake->read_calls;
    if (fake->read_status != vfs::Status::Ok) {
        return fake->read_status;
    }
    if (fake->overreport_io) {
        *bytes_read = size + 1;
        return vfs::Status::Ok;
    }
    FakeNode* node = file_node(fake, file);
    if (!node) {
        return vfs::Status::CorruptFilesystem;
    }
    if (offset >= node->size) {
        *bytes_read = 0;
        return vfs::Status::Ok;
    }
    size_t available = node->size - static_cast<size_t>(offset);
    size_t completed = size < available ? size : available;
    if (fake->short_io != 0 && completed > fake->short_io) {
        completed = fake->short_io;
    }
    copy_data(buffer, node->data + static_cast<size_t>(offset), completed);
    *bytes_read = completed;
    return vfs::Status::Ok;
}

vfs::Status fake_write(
    void* context,
    const vfs::BackendFile* file,
    uint64_t offset,
    const void* buffer,
    size_t size,
    size_t* bytes_written) {
    FakeFs* fake = static_cast<FakeFs*>(context);
    ++fake->write_calls;
    if (fake->write_status != vfs::Status::Ok) {
        return fake->write_status;
    }
    if (fake->overreport_io) {
        *bytes_written = size + 1;
        return vfs::Status::Ok;
    }
    FakeNode* node = file_node(fake, file);
    if (!node || offset > FAKE_MAX_DATA) {
        return vfs::Status::NoSpace;
    }
    size_t completed = size;
    if (fake->short_io != 0 && completed > fake->short_io) {
        completed = fake->short_io;
    }
    if (completed > FAKE_MAX_DATA - static_cast<size_t>(offset)) {
        return vfs::Status::NoSpace;
    }
    copy_data(node->data + static_cast<size_t>(offset), buffer, completed);
    const size_t end = static_cast<size_t>(offset) + completed;
    if (end > node->size) {
        node->size = end;
    }
    *bytes_written = completed;
    return vfs::Status::Ok;
}

vfs::Status fake_stat_open(
    void* context,
    const vfs::BackendFile* file,
    vfs::FileStat* info) {
    FakeFs* fake = static_cast<FakeFs*>(context);
    FakeNode* node = file_node(fake, file);
    if (!node) {
        return vfs::Status::CorruptFilesystem;
    }
    *info = {node->type, node->flags, static_cast<uint64_t>(node->size)};
    return vfs::Status::Ok;
}

bool direct_child(
    const char* directory,
    const char* candidate,
    const char** name) {
    const size_t directory_length = text_length(directory);
    if (text_equal(directory, "/")) {
        if (candidate[0] != '/' || candidate[1] == '\0') {
            return false;
        }
        *name = candidate + 1;
    } else {
        for (size_t i = 0; i < directory_length; ++i) {
            if (candidate[i] != directory[i]) {
                return false;
            }
        }
        if (candidate[directory_length] != '/' ||
            candidate[directory_length + 1] == '\0') {
            return false;
        }
        *name = candidate + directory_length + 1;
    }
    for (size_t i = 0; (*name)[i] != '\0'; ++i) {
        if ((*name)[i] == '/') {
            return false;
        }
    }
    return true;
}

vfs::Status fake_readdir(
    void* context,
    const vfs::BackendFile* directory,
    uint64_t cookie,
    vfs::DirectoryEntry* entry,
    uint64_t* next_cookie) {
    FakeFs* fake = static_cast<FakeFs*>(context);
    ++fake->readdir_calls;
    if (fake->readdir_status != vfs::Status::Ok) {
        return fake->readdir_status;
    }
    FakeNode* directory_node = file_node(fake, directory);
    if (!directory_node || directory_node->type != vfs::NodeType::Directory) {
        return vfs::Status::NotDirectory;
    }
    size_t start = cookie > FAKE_MAX_NODES
        ? FAKE_MAX_NODES
        : static_cast<size_t>(cookie);
    for (size_t i = start; i < FAKE_MAX_NODES; ++i) {
        FakeNode& node = fake->nodes[i];
        const char* name = nullptr;
        if (!node.active ||
            !direct_child(directory_node->path, node.path, &name)) {
            continue;
        }
        *entry = {};
        if (fake->invalid_directory_entry) {
            copy_text(entry->name, ".");
        } else {
            copy_text(entry->name, name);
        }
        entry->name_length = text_length(entry->name);
        entry->info = {
            node.type,
            node.flags,
            static_cast<uint64_t>(node.size)};
        *next_cookie = static_cast<uint64_t>(i + 1);
        return vfs::Status::Ok;
    }
    return vfs::Status::EndOfDirectory;
}

vfs::Status fake_create_common(
    FakeFs* fake,
    const char* path,
    vfs::NodeType type) {
    record_path(fake, path);
    if (fake->mutation_status != vfs::Status::Ok) {
        return fake->mutation_status;
    }
    if (find_node(fake, path)) {
        return vfs::Status::AlreadyExists;
    }
    return add_node(fake, path, type)
        ? vfs::Status::Ok
        : vfs::Status::NoSpace;
}

vfs::Status fake_create(void* context, const char* path) {
    FakeFs* fake = static_cast<FakeFs*>(context);
    ++fake->create_calls;
    return fake_create_common(fake, path, vfs::NodeType::Regular);
}

vfs::Status fake_mkdir(void* context, const char* path) {
    FakeFs* fake = static_cast<FakeFs*>(context);
    ++fake->mkdir_calls;
    return fake_create_common(fake, path, vfs::NodeType::Directory);
}

vfs::Status fake_remove_common(FakeFs* fake, const char* path) {
    record_path(fake, path);
    if (fake->mutation_status != vfs::Status::Ok) {
        return fake->mutation_status;
    }
    FakeNode* node = find_node(fake, path);
    if (!node) {
        return vfs::Status::NotFound;
    }
    node->active = false;
    return vfs::Status::Ok;
}

vfs::Status fake_unlink(void* context, const char* path) {
    FakeFs* fake = static_cast<FakeFs*>(context);
    ++fake->unlink_calls;
    return fake_remove_common(fake, path);
}

vfs::Status fake_rmdir(void* context, const char* path) {
    FakeFs* fake = static_cast<FakeFs*>(context);
    ++fake->rmdir_calls;
    return fake_remove_common(fake, path);
}

vfs::Status fake_rename(
    void* context,
    const char* source,
    const char* destination) {
    FakeFs* fake = static_cast<FakeFs*>(context);
    ++fake->rename_calls;
    record_path(fake, source);
    copy_text(fake->last_second_path, destination);
    if (fake->mutation_status != vfs::Status::Ok) {
        return fake->mutation_status;
    }
    FakeNode* node = find_node(fake, source);
    if (!node) {
        return vfs::Status::NotFound;
    }
    if (find_node(fake, destination)) {
        return vfs::Status::AlreadyExists;
    }
    copy_text(node->path, destination);
    return vfs::Status::Ok;
}

vfs::Status fake_sync(void* context) {
    FakeFs* fake = static_cast<FakeFs*>(context);
    ++fake->sync_calls;
    return fake->sync_status;
}

vfs::FileSystem filesystem(FakeFs* fake, bool read_only = false) {
    vfs::Operations operations = {};
    operations.stat_path = fake_stat;
    operations.open = fake_open;
    operations.close = fake_close;
    operations.read = fake_read;
    operations.write = fake_write;
    operations.stat_open = fake_stat_open;
    operations.readdir = fake_readdir;
    operations.create = fake_create;
    operations.unlink = fake_unlink;
    operations.rename = fake_rename;
    operations.mkdir = fake_mkdir;
    operations.rmdir = fake_rmdir;
    operations.sync = fake_sync;
    return {fake, operations, read_only};
}

void initialize_base(FakeFs* fake) {
    reset_fake(fake);
    add_node(fake, "/", vfs::NodeType::Directory);
    add_node(fake, "/mnt", vfs::NodeType::Directory);
    add_node(fake, "/mnt/nested", vfs::NodeType::Directory);
    add_node(fake, "/mnt/file", vfs::NodeType::Regular, "underlying");
    add_node(fake, "/mnt2", vfs::NodeType::Directory);
    add_node(fake, "/file", vfs::NodeType::Regular, "abcdef");
    add_node(fake, "/jail", vfs::NodeType::Directory);
    add_node(fake, "/jail/sub", vfs::NodeType::Directory);
    add_node(fake, "/jail/sub/item", vfs::NodeType::Regular, "item");
    add_node(fake, "/jail/escape", vfs::NodeType::Regular, "safe");
}

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf(                                                     \
                "check failed at line %d: %s\n", __LINE__, #condition);     \
            return false;                                                    \
        }                                                                    \
    } while (false)

bool initialize_vfs(
    vfs::State* state,
    vfs::PathContext* context,
    FakeFs* root) {
    initialize_base(root);
    const vfs::FileSystem root_filesystem = filesystem(root);
    return vfs::initialize(state, &root_filesystem) == vfs::Status::Ok &&
           vfs::initialize_path_context(state, context) == vfs::Status::Ok;
}

bool test_mount_routing_and_paths() {
    FakeFs root = {};
    vfs::State state = {};
    vfs::PathContext context = {};
    CHECK(initialize_vfs(&state, &context, &root));

    FakeFs first = {};
    reset_fake(&first);
    add_node(&first, "/", vfs::NodeType::Directory);
    add_node(&first, "/file", vfs::NodeType::Regular, "mounted");
    add_node(&first, "/nested", vfs::NodeType::Directory);
    FakeFs nested = {};
    reset_fake(&nested);
    add_node(&nested, "/", vfs::NodeType::Directory);
    add_node(&nested, "/deep", vfs::NodeType::Regular, "deep");

    vfs::MountHandle first_mount = {};
    vfs::FileSystem first_filesystem = filesystem(&first);
    CHECK(vfs::mount(
              &state, &context, "/mnt", &first_filesystem, &first_mount) ==
          vfs::Status::Ok);
    vfs::FileSystem nested_filesystem = filesystem(&nested);
    vfs::MountHandle nested_mount = {};
    CHECK(vfs::mount(
              &state,
              &context,
              "/mnt/nested",
              &nested_filesystem,
              &nested_mount) == vfs::Status::Ok);

    vfs::FileStat info = {};
    CHECK(vfs::stat(&state, &context, "/mnt/file", &info) ==
          vfs::Status::Ok);
    CHECK(text_equal(first.last_path, "/file"));
    CHECK(vfs::stat(&state, &context, "/mnt/nested/deep", &info) ==
          vfs::Status::Ok);
    CHECK(text_equal(nested.last_path, "/deep"));
    CHECK(vfs::stat(&state, &context, "/mnt2", &info) == vfs::Status::Ok);
    CHECK(text_equal(root.last_path, "/mnt2"));
    CHECK(vfs::stat(&state, &context, "/mnt/../mnt2", &info) ==
          vfs::Status::Ok);
    CHECK(text_equal(root.last_path, "/mnt2"));
    CHECK(vfs::stat(&state, &context, "/mnt", &info) == vfs::Status::Ok);
    CHECK(info.type == vfs::NodeType::MountPoint);

    CHECK(vfs::chdir(&state, &context, "/jail/sub") == vfs::Status::Ok);
    CHECK(vfs::stat(&state, &context, "../escape", &info) ==
          vfs::Status::Ok);
    CHECK(text_equal(root.last_path, "/jail/escape"));
    CHECK(vfs::chroot(&state, &context, "..") == vfs::Status::Ok);
    char cwd[32] = {};
    size_t required = 0;
    CHECK(vfs::getcwd(&context, cwd, sizeof(cwd), &required) ==
          vfs::Status::Ok);
    CHECK(text_equal(cwd, "/") && required == 2);
    CHECK(vfs::stat(&state, &context, "../../escape", &info) ==
          vfs::Status::Ok);
    CHECK(text_equal(root.last_path, "/jail/escape"));
    CHECK(vfs::chdir(&state, &context, "/sub") == vfs::Status::Ok);
    CHECK(vfs::getcwd(&context, cwd, sizeof(cwd), nullptr) ==
          vfs::Status::Ok);
    CHECK(text_equal(cwd, "/sub"));
    CHECK(vfs::stat(&state, &context, "/sub/item", &info) ==
          vfs::Status::Ok);
    CHECK(text_equal(root.last_path, "/jail/sub/item"));
    return true;
}

bool test_path_limits_and_transactional_outputs() {
    FakeFs root = {};
    vfs::State state = {};
    vfs::PathContext context = {};
    CHECK(initialize_vfs(&state, &context, &root));
    const size_t calls = root.stat_calls;

    char long_name[vfs::MAX_NAME_LENGTH + 3] = {};
    long_name[0] = '/';
    for (size_t i = 1; i <= vfs::MAX_NAME_LENGTH + 1; ++i) {
        long_name[i] = 'x';
    }
    vfs::FileStat sentinel = {
        vfs::NodeType::Pipe,
        vfs::NodeFlags::None,
        UINT64_C(0xABCDEF)};
    CHECK(vfs::stat(&state, &context, long_name, &sentinel) ==
          vfs::Status::NameTooLong);
    CHECK(sentinel.type == vfs::NodeType::Pipe &&
          sentinel.size == UINT64_C(0xABCDEF));

    char deep[96] = {};
    size_t position = 0;
    for (size_t i = 0; i < vfs::MAX_PATH_DEPTH + 1; ++i) {
        deep[position++] = '/';
        deep[position++] = 'a';
    }
    CHECK(vfs::stat(&state, &context, deep, &sentinel) ==
          vfs::Status::PathTooDeep);

    char too_long[vfs::MAX_PATH_LENGTH + 2] = {};
    too_long[0] = '/';
    for (size_t i = 1; i <= vfs::MAX_PATH_LENGTH; ++i) {
        too_long[i] = 'q';
    }
    CHECK(vfs::stat(&state, &context, too_long, &sentinel) ==
          vfs::Status::PathTooLong);
    CHECK(vfs::stat(&state, &context, "/file/", &sentinel) ==
          vfs::Status::NotDirectory);
    CHECK(root.stat_calls == calls + 1);

    char tiny[1] = {'z'};
    size_t required = 0;
    CHECK(vfs::getcwd(&context, tiny, sizeof(tiny), &required) ==
          vfs::Status::BufferTooSmall);
    CHECK(tiny[0] == 'z' && required == 2);
    return true;
}

bool test_open_io_seek_and_generations() {
    FakeFs root = {};
    vfs::State state = {};
    vfs::PathContext context = {};
    CHECK(initialize_vfs(&state, &context, &root));

    vfs::OpenFileHandle handle = {};
    CHECK(vfs::open(
              &state,
              &context,
              "/file",
              vfs::OpenFlags::Read | vfs::OpenFlags::Write,
              &handle) == vfs::Status::Ok);
    const size_t slot = static_cast<size_t>(handle.slot - 1);
    root.short_io = 2;
    char buffer[8] = {};
    size_t completed = 99;
    CHECK(vfs::read(&state, handle, buffer, 4, &completed) ==
          vfs::Status::Ok);
    CHECK(completed == 2 && text_equal(buffer, "ab"));
    CHECK(state.open_files[slot].offset == 2);

    root.read_status = vfs::Status::IoError;
    completed = 99;
    CHECK(vfs::read(&state, handle, buffer, 2, &completed) ==
          vfs::Status::IoError);
    CHECK(completed == 0 && state.open_files[slot].offset == 2);
    root.read_status = vfs::Status::Ok;
    root.overreport_io = true;
    CHECK(vfs::read(&state, handle, buffer, 2, &completed) ==
          vfs::Status::BackendFailure);
    CHECK(state.open_files[slot].offset == 2);
    root.overreport_io = false;

    uint64_t offset = 0;
    CHECK(vfs::seek(&state, handle, -2, vfs::SeekOrigin::End, &offset) ==
          vfs::Status::Ok);
    CHECK(offset == 4);
    CHECK(vfs::seek(&state, handle, -5, vfs::SeekOrigin::Begin, &offset) ==
          vfs::Status::OutOfRange);
    CHECK(state.open_files[slot].offset == 4);
    state.open_files[slot].offset = UINT64_MAX;
    const size_t reads_before_overflow = root.read_calls;
    CHECK(vfs::read(&state, handle, buffer, 1, &completed) ==
          vfs::Status::ArithmeticOverflow);
    CHECK(root.read_calls == reads_before_overflow);
    state.open_files[slot].offset = 0;

    root.short_io = 1;
    CHECK(vfs::write(&state, handle, "XY", 2, &completed) ==
          vfs::Status::Ok);
    CHECK(completed == 1 && state.open_files[slot].offset == 1);
    CHECK(vfs::retain(&state, handle) == vfs::Status::Ok);
    CHECK(vfs::close(&state, handle) == vfs::Status::Ok);
    CHECK(root.close_calls == 0);
    CHECK(vfs::close(&state, handle) == vfs::Status::Ok);
    CHECK(root.close_calls == 1);
    CHECK(vfs::close(&state, handle) == vfs::Status::StaleHandle);
    CHECK(root.close_calls == 1);

    vfs::OpenFileHandle replacement = {};
    CHECK(vfs::open(
              &state,
              &context,
              "/file",
              vfs::OpenFlags::Read,
              &replacement) == vfs::Status::Ok);
    CHECK(replacement.slot == handle.slot &&
          replacement.generation != handle.generation);
    CHECK(vfs::read(&state, handle, buffer, 1, nullptr) ==
          vfs::Status::StaleHandle);
    CHECK(vfs::close(&state, replacement) == vfs::Status::Ok);

    vfs::OpenFileHandle handles[vfs::MAX_OPEN_FILES] = {};
    for (size_t i = 0; i < vfs::MAX_OPEN_FILES; ++i) {
        CHECK(vfs::open(
                  &state,
                  &context,
                  "/file",
                  vfs::OpenFlags::Read,
                  &handles[i]) == vfs::Status::Ok);
    }
    const size_t stat_calls = root.stat_calls;
    const size_t open_calls = root.open_calls;
    vfs::OpenFileHandle overflow = {99, 99};
    CHECK(vfs::open(
              &state,
              &context,
              "/file",
              vfs::OpenFlags::Read,
              &overflow) == vfs::Status::OpenFileTableFull);
    CHECK(overflow.slot == 0 && overflow.generation == 0);
    CHECK(root.stat_calls == stat_calls && root.open_calls == open_calls);
    for (size_t i = 0; i < vfs::MAX_OPEN_FILES; ++i) {
        CHECK(vfs::close(&state, handles[i]) == vfs::Status::Ok);
    }
    return true;
}

bool test_readdir_and_mutations() {
    FakeFs root = {};
    vfs::State state = {};
    vfs::PathContext context = {};
    CHECK(initialize_vfs(&state, &context, &root));
    FakeFs mounted = {};
    reset_fake(&mounted);
    add_node(&mounted, "/", vfs::NodeType::Directory);
    vfs::FileSystem mounted_filesystem = filesystem(&mounted);
    CHECK(vfs::mount(
              &state, &context, "/mnt", &mounted_filesystem, nullptr) ==
          vfs::Status::Ok);

    vfs::OpenFileHandle directory = {};
    CHECK(vfs::open(
              &state,
              &context,
              "/",
              vfs::OpenFlags::Read | vfs::OpenFlags::Directory,
              &directory) == vfs::Status::Ok);
    root.invalid_directory_entry = true;
    vfs::DirectoryEntry entry = {};
    entry.name[0] = 'z';
    entry.name[1] = '\0';
    const size_t directory_slot = static_cast<size_t>(directory.slot - 1);
    CHECK(vfs::readdir(&state, directory, &entry) ==
          vfs::Status::BackendFailure);
    CHECK(entry.name[0] == 'z' &&
          state.open_files[directory_slot].offset == 0);
    root.invalid_directory_entry = false;
    CHECK(vfs::readdir(&state, directory, &entry) == vfs::Status::Ok);
    CHECK(text_equal(entry.name, "mnt"));
    CHECK(entry.info.type == vfs::NodeType::MountPoint);
    while (vfs::readdir(&state, directory, &entry) == vfs::Status::Ok) {
    }
    const uint64_t end_cookie = state.open_files[directory_slot].offset;
    entry.name[0] = 'q';
    CHECK(vfs::readdir(&state, directory, &entry) ==
          vfs::Status::EndOfDirectory);
    CHECK(entry.name[0] == 'q' &&
          state.open_files[directory_slot].offset == end_cookie);
    CHECK(vfs::close(&state, directory) == vfs::Status::Ok);

    CHECK(vfs::create(&state, &context, "/new") == vfs::Status::Ok);
    CHECK(root.create_calls == 1 && text_equal(root.last_path, "/new"));
    CHECK(vfs::mkdir(&state, &context, "/dir") == vfs::Status::Ok);
    CHECK(vfs::rename(&state, &context, "/new", "/renamed") ==
          vfs::Status::Ok);
    CHECK(root.rename_calls == 1 &&
          text_equal(root.last_path, "/new") &&
          text_equal(root.last_second_path, "/renamed"));
    CHECK(vfs::unlink(&state, &context, "/renamed") == vfs::Status::Ok);
    CHECK(vfs::rmdir(&state, &context, "/dir") == vfs::Status::Ok);
    CHECK(vfs::create(&state, &context, "/bad/") ==
          vfs::Status::InvalidPath);
    CHECK(vfs::mkdir(&state, &context, "/dot/.") ==
          vfs::Status::InvalidPath);
    return true;
}

bool test_busy_cross_device_readonly_and_rollback() {
    FakeFs root = {};
    vfs::State state = {};
    vfs::PathContext context = {};
    CHECK(initialize_vfs(&state, &context, &root));
    FakeFs first = {};
    reset_fake(&first);
    add_node(&first, "/", vfs::NodeType::Directory);
    add_node(&first, "/file", vfs::NodeType::Regular, "mounted");
    add_node(&first, "/nested", vfs::NodeType::Directory);
    FakeFs nested = {};
    reset_fake(&nested);
    add_node(&nested, "/", vfs::NodeType::Directory);

    vfs::FileSystem first_filesystem = filesystem(&first);
    vfs::FileSystem nested_filesystem = filesystem(&nested);
    vfs::MountHandle first_handle = {};
    vfs::MountHandle nested_handle = {};
    CHECK(vfs::mount(
              &state,
              &context,
              "/mnt",
              &first_filesystem,
              &first_handle) == vfs::Status::Ok);
    CHECK(vfs::mount(
              &state,
              &context,
              "/mnt/nested",
              &nested_filesystem,
              &nested_handle) == vfs::Status::Ok);
    CHECK(vfs::unmount(&state, first_handle) == vfs::Status::Busy);
    CHECK(vfs::rmdir(&state, &context, "/mnt") == vfs::Status::Busy);
    CHECK(vfs::rename(&state, &context, "/file", "/mnt/new") ==
          vfs::Status::CrossDevice);
    CHECK(root.rename_calls == 0 && first.rename_calls == 0);
    CHECK(vfs::unmount(&state, nested_handle) == vfs::Status::Ok);

    vfs::OpenFileHandle open_file = {};
    CHECK(vfs::open(
              &state,
              &context,
              "/mnt/file",
              vfs::OpenFlags::Read,
              &open_file) == vfs::Status::Ok);
    CHECK(vfs::unmount(&state, first_handle) == vfs::Status::Busy);
    CHECK(vfs::close(&state, open_file) == vfs::Status::Ok);
    first.sync_status = vfs::Status::IoError;
    CHECK(vfs::unmount(&state, first_handle) == vfs::Status::IoError);
    vfs::FileStat info = {};
    CHECK(vfs::stat(&state, &context, "/mnt/file", &info) ==
          vfs::Status::Ok);
    CHECK(text_equal(first.last_path, "/file"));
    first.sync_status = vfs::Status::Ok;
    CHECK(vfs::unmount(&state, first_handle) == vfs::Status::Ok);
    CHECK(vfs::stat(&state, &context, "/mnt/file", &info) ==
          vfs::Status::Ok);
    CHECK(text_equal(root.last_path, "/mnt/file"));
    CHECK(vfs::unmount(&state, first_handle) == vfs::Status::StaleHandle);

    FakeFs read_only = {};
    reset_fake(&read_only);
    add_node(&read_only, "/", vfs::NodeType::Directory);
    add_node(&read_only, "/file", vfs::NodeType::Regular, "ro");
    add_node(&root, "/ro", vfs::NodeType::Directory);
    vfs::FileSystem read_only_filesystem = filesystem(&read_only, true);
    CHECK(vfs::mount(
              &state,
              &context,
              "/ro",
              &read_only_filesystem,
              nullptr) == vfs::Status::Ok);
    CHECK(vfs::create(&state, &context, "/ro/new") ==
          vfs::Status::ReadOnly);
    CHECK(read_only.create_calls == 0);
    CHECK(vfs::open(
              &state,
              &context,
              "/ro/file",
              vfs::OpenFlags::Write,
              &open_file) == vfs::Status::ReadOnly);
    CHECK(read_only.open_calls == 0);

    add_node(&root, "/failed", vfs::NodeType::Directory);
    FakeFs failing = {};
    reset_fake(&failing);
    add_node(&failing, "/", vfs::NodeType::Directory);
    failing.stat_status = static_cast<vfs::Status>(UINT8_C(0xFF));
    vfs::FileSystem failing_filesystem = filesystem(&failing);
    vfs::MountHandle failed_handle = {9, 9};
    CHECK(vfs::mount(
              &state,
              &context,
              "/failed",
              &failing_filesystem,
              &failed_handle) == vfs::Status::BackendFailure);
    CHECK(failed_handle.slot == 0 && failed_handle.generation == 0);
    CHECK(vfs::stat(&state, &context, "/failed", &info) ==
          vfs::Status::Ok);
    CHECK(text_equal(root.last_path, "/failed"));
    return true;
}

bool test_capacity_missing_operations_and_initialization() {
    FakeFs root = {};
    initialize_base(&root);
    vfs::State failed_state = {};
    FakeFs bad_root = {};
    reset_fake(&bad_root);
    add_node(&bad_root, "/", vfs::NodeType::Regular);
    vfs::FileSystem bad_filesystem = filesystem(&bad_root);
    CHECK(vfs::initialize(&failed_state, &bad_filesystem) ==
          vfs::Status::NotDirectory);
    CHECK(!failed_state.initialized);

    vfs::State state = {};
    vfs::PathContext context = {};
    vfs::FileSystem root_filesystem = filesystem(&root);
    CHECK(vfs::initialize(&state, &root_filesystem) == vfs::Status::Ok);
    CHECK(vfs::initialize_path_context(&state, &context) == vfs::Status::Ok);
    FakeFs mounted = {};
    reset_fake(&mounted);
    add_node(&mounted, "/", vfs::NodeType::Directory);
    vfs::FileSystem mounted_filesystem = filesystem(&mounted);

    char path[16] = {};
    for (size_t i = 0; i < vfs::MAX_MOUNTS; ++i) {
        path[0] = '/';
        path[1] = 'm';
        path[2] = static_cast<char>('a' + static_cast<int>(i));
        path[3] = '\0';
        add_node(&root, path, vfs::NodeType::Directory);
    }
    for (size_t i = 0; i < vfs::MAX_MOUNTS - 1; ++i) {
        path[2] = static_cast<char>('a' + static_cast<int>(i));
        CHECK(vfs::mount(
                  &state,
                  &context,
                  path,
                  &mounted_filesystem,
                  nullptr) == vfs::Status::Ok);
    }
    const size_t root_stat_calls = root.stat_calls;
    const size_t mounted_stat_calls = mounted.stat_calls;
    path[2] = static_cast<char>(
        'a' + static_cast<int>(vfs::MAX_MOUNTS - 1));
    CHECK(vfs::mount(
              &state,
              &context,
              path,
              &mounted_filesystem,
              nullptr) == vfs::Status::MountTableFull);
    CHECK(root.stat_calls == root_stat_calls &&
          mounted.stat_calls == mounted_stat_calls);

    FakeFs limited = {};
    reset_fake(&limited);
    add_node(&limited, "/", vfs::NodeType::Directory);
    vfs::FileSystem limited_filesystem = filesystem(&limited);
    limited_filesystem.operations.create = nullptr;
    vfs::State limited_state = {};
    vfs::PathContext limited_context = {};
    CHECK(vfs::initialize(&limited_state, &limited_filesystem) ==
          vfs::Status::Ok);
    CHECK(vfs::initialize_path_context(
              &limited_state, &limited_context) == vfs::Status::Ok);
    CHECK(vfs::create(&limited_state, &limited_context, "/new") ==
          vfs::Status::Unsupported);
    limited_filesystem.operations.sync = nullptr;
    vfs::State no_sync_state = {};
    CHECK(vfs::initialize(&no_sync_state, &limited_filesystem) ==
          vfs::Status::Ok);
    CHECK(vfs::sync_all(&no_sync_state) == vfs::Status::Unsupported);
    return true;
}

} // namespace

int main() {
    struct TestCase {
        const char* name;
        bool (*run)();
    };
    const TestCase tests[] = {
        {"mount routing, cwd and chroot", test_mount_routing_and_paths},
        {"path limits and transactional outputs",
         test_path_limits_and_transactional_outputs},
        {"open I/O, seek and generations",
         test_open_io_seek_and_generations},
        {"readdir and mutations", test_readdir_and_mutations},
        {"busy, cross-device, read-only and rollback",
         test_busy_cross_device_readonly_and_rollback},
        {"capacity, missing operations and initialization",
         test_capacity_missing_operations_and_initialization},
    };

    for (const TestCase& test : tests) {
        if (!test.run()) {
            std::printf("FAIL: %s\n", test.name);
            return 1;
        }
        std::printf("PASS: %s\n", test.name);
    }
    return 0;
}
