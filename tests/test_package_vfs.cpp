#include "../kernel/fs/vfs.hpp"
#include "../kernel/install/package.hpp"
#include "../kernel/install/package_vfs.hpp"
#include "../kernel/libk/crc.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

constexpr size_t kHeaderSize = 64U;
constexpr size_t kEntrySize = 160U;

struct Record {
    const char* path;
    const char* data;
    uint32_t destination;
};

void put_u32(uint8_t* output, uint32_t value) {
    for (size_t index = 0U; index < 4U; ++index) {
        output[index] = static_cast<uint8_t>(value >> (index * 8U));
    }
}

void put_u64(uint8_t* output, uint64_t value) {
    put_u32(output, static_cast<uint32_t>(value));
    put_u32(output + 4U, static_cast<uint32_t>(value >> 32U));
}

std::vector<uint8_t> make_package(const Record* records, size_t count) {
    const size_t manifest_end = kHeaderSize + count * kEntrySize;
    const size_t data_offset = (manifest_end + 15U) & ~size_t{15U};
    size_t payload_size = 0U;
    for (size_t index = 0U; index < count; ++index) {
        payload_size += std::strlen(records[index].data);
    }
    std::vector<uint8_t> bytes(data_offset + payload_size, 0U);
    std::memcpy(bytes.data(), "KURPKG1", 7U);
    put_u32(bytes.data() + 8U, 1U);
    put_u32(bytes.data() + 12U, static_cast<uint32_t>(kHeaderSize));
    put_u64(bytes.data() + 16U, bytes.size());
    put_u32(bytes.data() + 24U, static_cast<uint32_t>(count));
    put_u32(bytes.data() + 28U, static_cast<uint32_t>(kEntrySize));
    put_u64(bytes.data() + 32U, kHeaderSize);
    put_u64(bytes.data() + 40U, data_offset);

    size_t cursor = data_offset;
    for (size_t index = 0U; index < count; ++index) {
        uint8_t* entry =
            bytes.data() + kHeaderSize + index * kEntrySize;
        const size_t path_length = std::strlen(records[index].path);
        std::memcpy(entry, records[index].path, path_length);
        const size_t file_size = std::strlen(records[index].data);
        std::memcpy(bytes.data() + cursor, records[index].data, file_size);
        put_u64(entry + 128U, cursor);
        put_u64(entry + 136U, file_size);
        put_u32(
            entry + 144U,
            k_crc32(bytes.data() + cursor, file_size));
        put_u32(entry + 148U, records[index].destination);
        cursor += file_size;
    }
    put_u32(
        bytes.data() + 48U,
        k_crc32(bytes.data() + kHeaderSize, count * kEntrySize));
    return bytes;
}

} // namespace

int main() {
    using namespace fs::vfs;
    using namespace install;

    const Record records[] = {
        {"/APPS/HELLO.ELF", "hello", package::DESTINATION_ROOT},
        {"/APPS/TOOLS/READ.ME", "readme", package::DESTINATION_ROOT},
        {"/README.TXT", "root", package::DESTINATION_ROOT},
        {"/SYSTEM/INIT.ELF", "init", package::DESTINATION_ROOT},
        {"/EFI/BOOTX64.EFI", "efi", package::DESTINATION_ESP},
    };
    std::vector<uint8_t> bytes = make_package(
        records, sizeof(records) / sizeof(records[0]));
    package::View package_view{};
    if (package::parse(bytes.data(), bytes.size(), &package_view) !=
        package::Status::Ok) {
        std::cerr << "live package fixture failed to parse\n";
        return 1;
    }

    package_vfs::Adapter adapter{};
    FileSystem filesystem{};
    State state{};
    PathContext path_context{};
    if (package_vfs::initialize(&adapter, package_view, &filesystem) !=
            Status::Ok ||
        initialize(&state, &filesystem) != Status::Ok ||
        initialize_path_context(&state, &path_context) != Status::Ok) {
        std::cerr << "read-only package VFS initialization failed\n";
        return 1;
    }

    const auto list_names = [&](const char* path,
                                const char* const* expected_names,
                                const NodeType* expected_types,
                                const uint64_t* expected_sizes,
                                size_t expected_count) {
        OpenFileHandle directory{};
        if (open(
                &state,
                &path_context,
                path,
                OpenFlags::Read | OpenFlags::Directory,
                &directory) != Status::Ok) {
            return false;
        }
        bool matches = true;
        for (size_t index = 0U; index < expected_count; ++index) {
            DirectoryEntry entry{};
            if (readdir(&state, directory, &entry) != Status::Ok ||
                std::strcmp(entry.name, expected_names[index]) != 0 ||
                entry.info.type != expected_types[index] ||
                entry.info.size != expected_sizes[index]) {
                matches = false;
                break;
            }
        }
        DirectoryEntry end{};
        if (matches &&
            readdir(&state, directory, &end) != Status::EndOfDirectory) {
            matches = false;
        }
        if (matches && expected_count != 0U) {
            uint64_t offset = UINT64_MAX;
            if (seek(
                    &state,
                    directory,
                    0,
                    SeekOrigin::Begin,
                    &offset) != Status::Ok ||
                offset != 0U) {
                matches = false;
            } else {
                DirectoryEntry first{};
                matches = readdir(&state, directory, &first) == Status::Ok &&
                    std::strcmp(first.name, expected_names[0]) == 0;
            }
        }
        if (close(&state, directory) != Status::Ok) matches = false;
        return matches;
    };

    const char* root_names[] = {"APPS", "README.TXT", "SYSTEM"};
    const NodeType root_types[] = {
        NodeType::Directory, NodeType::Regular, NodeType::Directory};
    const uint64_t root_sizes[] = {0U, 4U, 0U};
    if (!list_names(
            "/", root_names, root_types, root_sizes,
            sizeof(root_names) / sizeof(root_names[0]))) {
        std::cerr << "root listing, deduplication, or seek failed\n";
        return 1;
    }

    const char* apps_names[] = {"HELLO.ELF", "TOOLS"};
    const NodeType apps_types[] = {NodeType::Regular, NodeType::Directory};
    const uint64_t apps_sizes[] = {5U, 0U};
    if (!list_names(
            "/APPS", apps_names, apps_types, apps_sizes,
            sizeof(apps_names) / sizeof(apps_names[0]))) {
        std::cerr << "nested package directory listing failed\n";
        return 1;
    }

    const char* tool_names[] = {"READ.ME"};
    const NodeType tool_types[] = {NodeType::Regular};
    const uint64_t tool_sizes[] = {6U};
    if (!list_names(
            "/APPS/TOOLS", tool_names, tool_types, tool_sizes, 1U)) {
        std::cerr << "deep package directory listing failed\n";
        return 1;
    }

    if (create(&state, &path_context, "/NEW.TXT") != Status::ReadOnly) {
        std::cerr << "live package unexpectedly accepted a mutation\n";
        return 1;
    }
    std::cout << "read-only live package VFS directory enumeration: PASS\n";
    return 0;
}
