#include "../kernel/user/elf_loader.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

namespace {

struct [[gnu::packed]] Header {
    uint8_t identification[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_offset;
    uint64_t section_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_entry_size;
    uint16_t program_count;
    uint16_t section_entry_size;
    uint16_t section_count;
    uint16_t section_name_index;
};

struct [[gnu::packed]] Program {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
};

using ImageBytes = std::array<uint8_t, 3U * 4096U>;

void make_valid(ImageBytes& bytes) {
    bytes.fill(0);
    auto* header = reinterpret_cast<Header*>(bytes.data());
    header->identification[0] = 0x7F;
    header->identification[1] = 'E';
    header->identification[2] = 'L';
    header->identification[3] = 'F';
    header->identification[4] = 2;
    header->identification[5] = 1;
    header->identification[6] = 1;
    header->type = 2;
    header->machine = 62;
    header->version = 1;
    header->entry = user::elf::USER_REGION_BASE;
    header->program_offset = sizeof(Header);
    header->header_size = sizeof(Header);
    header->program_entry_size = sizeof(Program);
    header->program_count = 1;

    auto* program = reinterpret_cast<Program*>(
        bytes.data() + sizeof(Header));
    program->type = 1;
    program->flags = 5;
    program->offset = 4096;
    program->virtual_address = user::elf::USER_REGION_BASE;
    program->file_size = 1;
    program->memory_size = 1;
    program->alignment = 4096;
    bytes[4096] = 0xC3;
}

} // namespace

int main() {
    ImageBytes bytes{};
    make_valid(bytes);
    uint64_t entry = 0;
    assert(user::elf::validate(bytes.data(), bytes.size(), &entry) ==
           user::elf::Status::Ok);
    assert(entry == user::elf::USER_REGION_BASE);

    auto* header = reinterpret_cast<Header*>(bytes.data());
    auto* program = reinterpret_cast<Program*>(
        bytes.data() + sizeof(Header));

    const uint8_t magic = header->identification[0];
    header->identification[0] = 0;
    assert(user::elf::validate(bytes.data(), bytes.size()) ==
           user::elf::Status::InvalidMagic);
    header->identification[0] = magic;

    program->flags = 7;
    assert(user::elf::validate(bytes.data(), bytes.size()) ==
           user::elf::Status::WritableExecutableSegment);
    program->flags = 5;

    header->entry = user::elf::USER_REGION_BASE + 4096;
    assert(user::elf::validate(bytes.data(), bytes.size()) ==
           user::elf::Status::InvalidEntry);
    header->entry = user::elf::USER_REGION_BASE;

    program->offset = bytes.size();
    assert(user::elf::validate(bytes.data(), bytes.size()) ==
           user::elf::Status::SegmentOutOfRange);
    program->offset = 4096;

    header->program_count = 2;
    auto* second = program + 1;
    *second = *program;
    second->offset = 8192;
    assert(user::elf::validate(bytes.data(), bytes.size()) ==
           user::elf::Status::SegmentOverlap);

    return 0;
}
