#include "elf_loader.hpp"

#include "../memory/physical_memory.hpp"

namespace user::elf {
namespace {

constexpr size_t kIdentificationSize = 16U;
constexpr uint8_t kElfClass64 = 2U;
constexpr uint8_t kElfDataLittleEndian = 1U;
constexpr uint8_t kElfVersion = 1U;
constexpr uint16_t kExecutable = 2U;
constexpr uint16_t kMachineX86_64 = 62U;
constexpr uint32_t kProgramLoad = 1U;
constexpr uint32_t kProgramDynamic = 2U;
constexpr uint32_t kProgramInterpreter = 3U;
constexpr uint32_t kProgramNote = 4U;
constexpr uint32_t kProgramHeader = 6U;
constexpr uint32_t kProgramTls = 7U;
constexpr uint32_t kProgramGnuStack = UINT32_C(0x6474E551);
constexpr uint32_t kFlagExecute = 1U;
constexpr uint32_t kFlagWrite = 2U;
constexpr uint32_t kKnownSegmentFlags = 7U;
constexpr size_t kMaximumProgramHeaders = 32U;
constexpr uint64_t kPageMask = memory::virtual_memory::PAGE_SIZE - 1U;

struct [[gnu::packed]] Header {
    uint8_t identification[kIdentificationSize];
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

struct [[gnu::packed]] ProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
};

static_assert(sizeof(Header) == 64U, "ELF64 header layout mismatch");
static_assert(
    sizeof(ProgramHeader) == 56U,
    "ELF64 program-header layout mismatch");

bool add_overflows(uint64_t left, uint64_t right, uint64_t* result) {
    if (left > UINT64_MAX - right) {
        return true;
    }
    *result = left + right;
    return false;
}

bool multiply_overflows(size_t left, size_t right, size_t* result) {
    if (left != 0U && right > SIZE_MAX / left) {
        return true;
    }
    *result = left * right;
    return false;
}

bool is_power_of_two(uint64_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

uint64_t align_down(uint64_t value) {
    return value & ~kPageMask;
}

bool align_up(uint64_t value, uint64_t* result) {
    uint64_t adjusted = 0U;
    if (add_overflows(value, kPageMask, &adjusted)) {
        return false;
    }
    *result = adjusted & ~kPageMask;
    return true;
}

const Header* header_from(const void* bytes) {
    return static_cast<const Header*>(bytes);
}

const ProgramHeader* programs_from(const void* bytes, const Header& header) {
    return reinterpret_cast<const ProgramHeader*>(
        static_cast<const uint8_t*>(bytes) + header.program_offset);
}

Status validate_header(const void* bytes, size_t size, const Header** output) {
    if (bytes == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }
    if (size < sizeof(Header)) {
        return Status::ImageTooSmall;
    }
    const Header* header = header_from(bytes);
    if (header->identification[0] != 0x7FU ||
        header->identification[1] != 'E' ||
        header->identification[2] != 'L' ||
        header->identification[3] != 'F') {
        return Status::InvalidMagic;
    }
    if (header->identification[4] != kElfClass64) {
        return Status::UnsupportedClass;
    }
    if (header->identification[5] != kElfDataLittleEndian ||
        header->identification[6] != kElfVersion) {
        return Status::UnsupportedEncoding;
    }
    if (header->type != kExecutable) {
        return Status::UnsupportedType;
    }
    if (header->machine != kMachineX86_64) {
        return Status::UnsupportedMachine;
    }
    if (header->version != kElfVersion ||
        header->header_size != sizeof(Header) ||
        header->program_entry_size != sizeof(ProgramHeader)) {
        return Status::InvalidHeader;
    }
    if (header->program_count == 0U ||
        header->program_count > kMaximumProgramHeaders ||
        header->program_offset < sizeof(Header)) {
        return Status::InvalidProgramTable;
    }
    size_t table_size = 0U;
    if (multiply_overflows(
            header->program_count, sizeof(ProgramHeader), &table_size) ||
        header->program_offset > size ||
        table_size > size - static_cast<size_t>(header->program_offset)) {
        return Status::InvalidProgramTable;
    }
    *output = header;
    return Status::Ok;
}

Status validate_programs(
    const void* bytes,
    size_t size,
    const Header& header) {
    const ProgramHeader* programs = programs_from(bytes, header);
    bool found_load = false;
    bool entry_is_executable = false;
    size_t page_count = 0U;

    for (size_t index = 0U; index < header.program_count; ++index) {
        const ProgramHeader& program = programs[index];
        if (program.type == 0U || program.type == kProgramNote ||
            program.type == kProgramHeader ||
            program.type == kProgramGnuStack) {
            continue;
        }
        if (program.type == kProgramDynamic ||
            program.type == kProgramInterpreter ||
            program.type == kProgramTls || program.type != kProgramLoad) {
            return Status::UnsupportedProgram;
        }
        if (program.memory_size == 0U) {
            continue;
        }
        found_load = true;
        if ((program.flags & ~kKnownSegmentFlags) != 0U ||
            program.file_size > program.memory_size ||
            (program.alignment != 0U &&
             (!is_power_of_two(program.alignment) ||
              program.alignment > memory::virtual_memory::PAGE_SIZE)) ||
            ((program.virtual_address - program.offset) & kPageMask) != 0U) {
            return Status::InvalidSegment;
        }
        if ((program.flags & (kFlagWrite | kFlagExecute)) ==
            (kFlagWrite | kFlagExecute)) {
            return Status::WritableExecutableSegment;
        }

        uint64_t file_end = 0U;
        uint64_t memory_end = 0U;
        if (add_overflows(program.offset, program.file_size, &file_end) ||
            file_end > size ||
            add_overflows(
                program.virtual_address,
                program.memory_size,
                &memory_end) ||
            program.virtual_address < USER_REGION_BASE ||
            memory_end > USER_REGION_END ||
            memory_end <= program.virtual_address) {
            return Status::SegmentOutOfRange;
        }

        uint64_t mapped_end = 0U;
        if (!align_up(memory_end, &mapped_end)) {
            return Status::SegmentOutOfRange;
        }
        const uint64_t mapped_begin = align_down(program.virtual_address);
        const uint64_t pages =
            (mapped_end - mapped_begin) / memory::virtual_memory::PAGE_SIZE;
        if (pages > MAX_IMAGE_PAGES - page_count) {
            return Status::TooManyPages;
        }
        page_count += static_cast<size_t>(pages);

        for (size_t previous = 0U; previous < index; ++previous) {
            const ProgramHeader& other = programs[previous];
            if (other.type != kProgramLoad || other.memory_size == 0U) {
                continue;
            }
            uint64_t other_end = 0U;
            if (add_overflows(
                    other.virtual_address,
                    other.memory_size,
                    &other_end)) {
                return Status::SegmentOutOfRange;
            }
            if (program.virtual_address < other_end &&
                other.virtual_address < memory_end) {
                return Status::SegmentOverlap;
            }
            uint64_t other_mapped_end = 0U;
            if (!align_up(other_end, &other_mapped_end)) {
                return Status::SegmentOutOfRange;
            }
            if (mapped_begin < other_mapped_end &&
                align_down(other.virtual_address) < mapped_end) {
                return Status::SegmentOverlap;
            }
        }

        if ((program.flags & kFlagExecute) != 0U &&
            header.entry >= program.virtual_address &&
            header.entry < memory_end) {
            entry_is_executable = true;
        }
    }

    return found_load && entry_is_executable
        ? Status::Ok
        : Status::InvalidEntry;
}

void zero_page(void* page) {
    auto* bytes = static_cast<uint8_t*>(page);
    for (size_t index = 0U;
         index < memory::virtual_memory::PAGE_SIZE;
         ++index) {
        bytes[index] = 0U;
    }
}

void copy_bytes(void* destination, const void* source, size_t size) {
    auto* output = static_cast<uint8_t*>(destination);
    const auto* input = static_cast<const uint8_t*>(source);
    for (size_t index = 0U; index < size; ++index) {
        output[index] = input[index];
    }
}

Status map_page(
    Image* image,
    uint64_t virtual_address,
    memory::virtual_memory::MapFlags flags,
    const void* initial_data,
    size_t initial_offset,
    size_t initial_size) {
    if (image == nullptr || image->address_space == nullptr ||
        image->page_count >= MAX_IMAGE_PAGES ||
        (virtual_address & kPageMask) != 0U ||
        virtual_address < USER_REGION_BASE ||
        virtual_address >= USER_REGION_END ||
        initial_offset > memory::virtual_memory::PAGE_SIZE ||
        initial_size > memory::virtual_memory::PAGE_SIZE - initial_offset ||
        (initial_size != 0U && initial_data == nullptr)) {
        return Status::InvalidArgument;
    }

    void* frame = memory::alloc_frame();
    if (frame == nullptr) {
        return Status::OutOfMemory;
    }
    zero_page(frame);
    if (initial_size != 0U) {
        copy_bytes(
            static_cast<uint8_t*>(frame) + initial_offset,
            initial_data,
            initial_size);
    }

    flags |= memory::virtual_memory::MapFlags::User;
    const memory::virtual_memory::Status map_status =
        memory::virtual_memory::map_page(
            image->address_space,
            virtual_address,
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(frame)),
            flags);
    if (map_status != memory::virtual_memory::Status::Ok) {
        memory::free_frame(frame);
        return Status::MappingFailed;
    }
    image->pages[image->page_count++] = {virtual_address, frame};
    return Status::Ok;
}

} // namespace

Status validate(const void* bytes, size_t size, uint64_t* entry) {
    if (entry != nullptr) {
        *entry = 0U;
    }
    const Header* header = nullptr;
    Status status = validate_header(bytes, size, &header);
    if (status != Status::Ok) {
        return status;
    }
    status = validate_programs(bytes, size, *header);
    if (status == Status::Ok && entry != nullptr) {
        *entry = header->entry;
    }
    return status;
}

Status load(
    const void* bytes,
    size_t size,
    memory::virtual_memory::AddressSpace* address_space,
    Image* output) {
    if (output == nullptr || address_space == nullptr ||
        !address_space->initialized) {
        return Status::InvalidArgument;
    }
    *output = {};
    uint64_t entry = 0U;
    const Status validation = validate(bytes, size, &entry);
    if (validation != Status::Ok) {
        return validation;
    }
    output->address_space = address_space;
    output->entry = entry;

    const Header& header = *header_from(bytes);
    const ProgramHeader* programs = programs_from(bytes, header);
    for (size_t index = 0U; index < header.program_count; ++index) {
        const ProgramHeader& program = programs[index];
        if (program.type != kProgramLoad || program.memory_size == 0U) {
            continue;
        }
        uint64_t memory_end = program.virtual_address + program.memory_size;
        uint64_t mapped_end = 0U;
        static_cast<void>(align_up(memory_end, &mapped_end));
        const uint64_t file_end_virtual =
            program.virtual_address + program.file_size;
        memory::virtual_memory::MapFlags flags =
            memory::virtual_memory::MapFlags::None;
        if ((program.flags & kFlagWrite) != 0U) {
            flags |= memory::virtual_memory::MapFlags::Writable;
        }
        if ((program.flags & kFlagExecute) == 0U) {
            flags |= memory::virtual_memory::MapFlags::NoExecute;
        }

        for (uint64_t page = align_down(program.virtual_address);
             page < mapped_end;
             page += memory::virtual_memory::PAGE_SIZE) {
            const uint64_t copy_begin =
                page > program.virtual_address ? page : program.virtual_address;
            const uint64_t page_end =
                page + memory::virtual_memory::PAGE_SIZE;
            const uint64_t copy_end = page_end < file_end_virtual
                ? page_end
                : file_end_virtual;
            const size_t copy_size = copy_end > copy_begin
                ? static_cast<size_t>(copy_end - copy_begin)
                : 0U;
            const size_t source_offset = static_cast<size_t>(
                program.offset + (copy_begin - program.virtual_address));
            const Status map_status = map_page(
                output,
                page,
                flags,
                copy_size == 0U
                    ? nullptr
                    : static_cast<const uint8_t*>(bytes) + source_offset,
                static_cast<size_t>(copy_begin - page),
                copy_size);
            if (map_status != Status::Ok) {
                static_cast<void>(unload(output));
                return map_status;
            }
        }
    }
    output->loaded = true;
    return Status::Ok;
}

Status map_anonymous_page(
    Image* image,
    uint64_t virtual_address,
    memory::virtual_memory::MapFlags flags,
    const void* initial_data,
    size_t initial_size) {
    if (image == nullptr || !image->loaded) {
        return Status::InvalidArgument;
    }
    return map_page(
        image,
        virtual_address,
        flags,
        initial_data,
        0U,
        initial_size);
}

Status unmap_owned_page(Image* image, uint64_t virtual_address) {
    if (image == nullptr || image->address_space == nullptr ||
        (virtual_address & kPageMask) != 0U) {
        return Status::InvalidArgument;
    }
    size_t index = image->page_count;
    for (size_t candidate = 0U; candidate < image->page_count; ++candidate) {
        if (image->pages[candidate].virtual_address == virtual_address) {
            index = candidate;
            break;
        }
    }
    if (index == image->page_count) return Status::InvalidArgument;

    const Page page = image->pages[index];
    memory::virtual_memory::Mapping removed{};
    if (memory::virtual_memory::unmap_page(
            image->address_space,
            page.virtual_address,
            &removed) != memory::virtual_memory::Status::Ok ||
        removed.physical_address != static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(page.physical_frame))) {
        return Status::UnmapFailed;
    }
    memory::free_frame(page.physical_frame);
    --image->page_count;
    image->pages[index] = image->pages[image->page_count];
    image->pages[image->page_count] = {};
    return Status::Ok;
}

Status unload(Image* image) {
    if (image == nullptr || image->address_space == nullptr) {
        return Status::InvalidArgument;
    }
    Status result = Status::Ok;
    while (image->page_count != 0U) {
        const Page page = image->pages[--image->page_count];
        memory::virtual_memory::Mapping removed{};
        const memory::virtual_memory::Status status =
            memory::virtual_memory::unmap_page(
                image->address_space,
                page.virtual_address,
                &removed);
        if (status != memory::virtual_memory::Status::Ok ||
            removed.physical_address != static_cast<uint64_t>(
                reinterpret_cast<uintptr_t>(page.physical_frame))) {
            result = Status::UnmapFailed;
            continue;
        }
        memory::free_frame(page.physical_frame);
    }
    image->loaded = false;
    image->entry = 0U;
    image->address_space = nullptr;
    return result;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::InvalidArgument: return "invalid argument";
        case Status::ImageTooSmall: return "ELF image is too small";
        case Status::InvalidMagic: return "invalid ELF magic";
        case Status::UnsupportedClass: return "ELF is not 64-bit";
        case Status::UnsupportedEncoding:
            return "unsupported ELF encoding or version";
        case Status::UnsupportedType: return "ELF is not ET_EXEC";
        case Status::UnsupportedMachine: return "ELF is not x86-64";
        case Status::InvalidHeader: return "invalid ELF header";
        case Status::InvalidProgramTable: return "invalid program table";
        case Status::UnsupportedProgram:
            return "unsupported dynamic/interpreted ELF program";
        case Status::InvalidSegment: return "invalid load segment";
        case Status::SegmentOutOfRange: return "load segment is out of range";
        case Status::SegmentOverlap: return "load segments overlap";
        case Status::WritableExecutableSegment:
            return "writable executable segment rejected";
        case Status::InvalidEntry: return "entry is not executable";
        case Status::TooManyPages: return "ELF maps too many pages";
        case Status::OutOfMemory: return "out of physical memory";
        case Status::MappingFailed: return "user page mapping failed";
        case Status::UnmapFailed: return "user page unmap failed";
    }
    return "unknown ELF loader status";
}

} // namespace user::elf
