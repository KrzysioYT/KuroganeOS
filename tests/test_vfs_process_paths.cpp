#include "../kernel/fs/vfs.hpp"

#include <cstdio>

namespace vfs = fs::vfs;

namespace {

struct FakeFs {
    char last_path[vfs::MAX_PATH_LENGTH + 1U];
};

bool text_equal(const char* left, const char* right) {
    size_t index = 0U;
    while (left[index] != '\0' || right[index] != '\0') {
        if (left[index] != right[index]) return false;
        ++index;
    }
    return true;
}

void copy_text(char* destination, const char* source) {
    size_t index = 0U;
    do {
        destination[index] = source[index];
    } while (source[index++] != '\0');
}

bool is_directory(const char* path) {
    return text_equal(path, "/") || text_equal(path, "/alpha") ||
        text_equal(path, "/alpha/work") || text_equal(path, "/beta");
}

bool is_file(const char* path) {
    return text_equal(path, "/alpha/file") || text_equal(path, "/beta/file");
}

vfs::Status fake_stat(void* context, const char* path, vfs::FileStat* info) {
    auto* fake = static_cast<FakeFs*>(context);
    copy_text(fake->last_path, path);
    if (is_directory(path)) {
        *info = {vfs::NodeType::Directory, vfs::NodeFlags::None, 0U};
        return vfs::Status::Ok;
    }
    if (is_file(path)) {
        *info = {vfs::NodeType::Regular, vfs::NodeFlags::Seekable, 4U};
        return vfs::Status::Ok;
    }
    return vfs::Status::NotFound;
}

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("check failed at line %d: %s\n", __LINE__, #condition); \
            return 1;                                                        \
        }                                                                    \
    } while (false)

} // namespace

int main() {
    FakeFs fake{};
    vfs::Operations operations{};
    operations.stat_path = fake_stat;
    const vfs::FileSystem filesystem{&fake, operations, false};

    vfs::State state{};
    CHECK(vfs::initialize(&state, &filesystem) == vfs::Status::Ok);

    vfs::PathContext first{};
    vfs::PathContext second{};
    CHECK(vfs::initialize_path_context(&state, &first) == vfs::Status::Ok);
    CHECK(vfs::initialize_path_context(&state, &second) == vfs::Status::Ok);

    CHECK(vfs::chdir(&state, &first, "/alpha/work") == vfs::Status::Ok);
    CHECK(vfs::chdir(&state, &second, "/beta") == vfs::Status::Ok);

    char cwd[vfs::MAX_PATH_LENGTH + 1U]{};
    size_t required = 0U;
    CHECK(vfs::getcwd(&first, cwd, sizeof(cwd), &required) == vfs::Status::Ok);
    CHECK(text_equal(cwd, "/alpha/work"));
    CHECK(required == 12U);
    CHECK(vfs::getcwd(&second, cwd, sizeof(cwd), nullptr) == vfs::Status::Ok);
    CHECK(text_equal(cwd, "/beta"));

    vfs::FileStat info{};
    CHECK(vfs::stat(&state, &first, "../file", &info) == vfs::Status::Ok);
    CHECK(text_equal(fake.last_path, "/alpha/file"));
    CHECK(info.type == vfs::NodeType::Regular);

    CHECK(vfs::stat(&state, &second, "file", &info) == vfs::Status::Ok);
    CHECK(text_equal(fake.last_path, "/beta/file"));

    CHECK(vfs::chdir(&state, &first, "..") == vfs::Status::Ok);
    CHECK(vfs::getcwd(&first, cwd, sizeof(cwd), nullptr) == vfs::Status::Ok);
    CHECK(text_equal(cwd, "/alpha"));
    CHECK(vfs::getcwd(&second, cwd, sizeof(cwd), nullptr) == vfs::Status::Ok);
    CHECK(text_equal(cwd, "/beta"));

    CHECK(vfs::chdir(&state, &first, "../../../../") == vfs::Status::Ok);
    CHECK(vfs::getcwd(&first, cwd, sizeof(cwd), nullptr) == vfs::Status::Ok);
    CHECK(text_equal(cwd, "/"));
    CHECK(vfs::getcwd(&second, cwd, sizeof(cwd), nullptr) == vfs::Status::Ok);
    CHECK(text_equal(cwd, "/beta"));

    required = 0U;
    CHECK(vfs::getcwd(&second, nullptr, 0U, &required) == vfs::Status::BufferTooSmall);
    CHECK(required == 6U);

    std::puts("process-local VFS path-context tests passed");
    return 0;
}
