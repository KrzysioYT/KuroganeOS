#include "uefi.h"
#include "../include/elf.h"
#include "../../common/boot_protocol.h"
#include "../../common/version.h"

#define PAGE_MASK UINT64_C(0xFFF)
#define MAP_SLACK_DESCRIPTORS 64u
#define MAX_DESCRIPTOR_SIZE 4096u
#define MAX_KERNEL_PROGRAM_HEADERS 64u
#define MAX_KERNEL_RELOCATIONS 65536u

typedef struct {
    Elf64_Addr entry;
    EFI_PHYSICAL_ADDRESS start;
    EFI_PHYSICAL_ADDRESS end;
} LoadedKernel;

typedef struct {
    EFI_MEMORY_DESCRIPTOR* data;
    UINTN capacity;
    UINTN size;
    UINTN key;
    UINTN descriptor_size;
    UINT32 descriptor_version;
} RawMemoryMap;

void* memset(void* destination, int value, size_t count) {
    UINT8* bytes = (UINT8*)destination;
    while (count--) {
        *bytes++ = (UINT8)value;
    }
    return destination;
}

void* memcpy(void* destination, const void* source, size_t count) {
    UINT8* output = (UINT8*)destination;
    const UINT8* input = (const UINT8*)source;
    while (count--) {
        *output++ = *input++;
    }
    return destination;
}

static BOOLEAN guid_equal(const EFI_GUID* left, const EFI_GUID* right) {
    const UINT8* lhs = (const UINT8*)left;
    const UINT8* rhs = (const UINT8*)right;
    UINTN index;
    for (index = 0; index < sizeof(EFI_GUID); ++index) {
        if (lhs[index] != rhs[index]) {
            return FALSE;
        }
    }
    return TRUE;
}

static void console_write(EFI_SYSTEM_TABLE* system_table,
                          const CHAR16* text) {
    if (system_table && system_table->ConOut &&
        system_table->ConOut->OutputString && text) {
        system_table->ConOut->OutputString(system_table->ConOut, text);
    }
}

static BOOLEAN range_inside(UINT64 offset, UINT64 length, UINT64 total) {
    return offset <= total && length <= total - offset;
}

static BOOLEAN virtual_range_in_load(
    const Elf64_Phdr* programs,
    UINTN program_count,
    UINT64 address,
    UINT64 length,
    UINT32 required_flags,
    UINT32 forbidden_flags
) {
    UINTN index;
    if (!programs || length == 0) {
        return FALSE;
    }
    for (index = 0; index < program_count; ++index) {
        const Elf64_Phdr* program = &programs[index];
        UINT64 delta;
        if (program->p_type != PT_LOAD ||
            address < program->p_vaddr) {
            continue;
        }
        delta = address - program->p_vaddr;
        if (delta <= program->p_memsz &&
            length <= program->p_memsz - delta &&
            (program->p_flags & required_flags) == required_flags &&
            (program->p_flags & forbidden_flags) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOLEAN virtual_range_to_file(
    const Elf64_Phdr* programs,
    UINTN program_count,
    UINT64 address,
    UINT64 length,
    UINT64 file_size,
    UINT64* output_offset
) {
    UINTN index;
    if (!programs || !output_offset || length == 0) {
        return FALSE;
    }
    for (index = 0; index < program_count; ++index) {
        const Elf64_Phdr* program = &programs[index];
        UINT64 delta;
        UINT64 offset;
        if (program->p_type != PT_LOAD ||
            address < program->p_vaddr) {
            continue;
        }
        delta = address - program->p_vaddr;
        if (delta > program->p_filesz ||
            length > program->p_filesz - delta ||
            program->p_offset > UINT64_MAX - delta) {
            continue;
        }
        offset = program->p_offset + delta;
        if (!range_inside(offset, length, file_size)) {
            continue;
        }
        *output_offset = offset;
        return TRUE;
    }
    return FALSE;
}

static BOOLEAN unsupported_dynamic_tag(Elf64_Sxword tag) {
    switch (tag) {
    case DT_NEEDED:
    case DT_PLTRELSZ:
    case DT_PLTGOT:
    case DT_INIT:
    case DT_FINI:
    case DT_REL:
    case DT_RELSZ:
    case DT_RELENT:
    case DT_PLTREL:
    case DT_TEXTREL:
    case DT_JMPREL:
    case DT_INIT_ARRAY:
    case DT_FINI_ARRAY:
    case DT_INIT_ARRAYSZ:
    case DT_FINI_ARRAYSZ:
    case DT_RELRSZ:
    case DT_RELR:
    case DT_RELRENT:
        return TRUE;
    default:
        return FALSE;
    }
}

static EFI_STATUS apply_dynamic_relocations(
    const VOID* file,
    UINTN file_size,
    const Elf64_Phdr* programs,
    UINTN program_count,
    const Elf64_Phdr* dynamic_program,
    EFI_PHYSICAL_ADDRESS image_base,
    UINT64 image_size
) {
    const Elf64_Dyn* dynamic_entries;
    const Elf64_Rela* relocations;
    UINT64 relocation_address = 0;
    UINT64 relocation_size = 0;
    UINT64 relocation_entry_size = 0;
    UINT64 advertised_relative_count = 0;
    UINT64 relocation_file_offset;
    UINT64 relocation_count;
    UINT64 previous_target_end = 0;
    BOOLEAN have_relocation_address = FALSE;
    BOOLEAN have_relocation_size = FALSE;
    BOOLEAN have_relocation_entry_size = FALSE;
    BOOLEAN have_relative_count = FALSE;
    BOOLEAN found_null = FALSE;
    UINTN dynamic_count;
    UINTN index;

    if (!file || !programs || !dynamic_program ||
        dynamic_program->p_filesz == 0 ||
        dynamic_program->p_filesz != dynamic_program->p_memsz ||
        dynamic_program->p_filesz % sizeof(Elf64_Dyn) != 0 ||
        !range_inside(
            dynamic_program->p_offset,
            dynamic_program->p_filesz,
            file_size)) {
        return EFI_LOAD_ERROR;
    }

    dynamic_entries = (const Elf64_Dyn*)(
        (const UINT8*)file + dynamic_program->p_offset);
    dynamic_count =
        (UINTN)(dynamic_program->p_filesz / sizeof(Elf64_Dyn));

    for (index = 0; index < dynamic_count; ++index) {
        const Elf64_Dyn* entry = &dynamic_entries[index];
        if (found_null) {
            if (entry->d_tag != DT_NULL) {
                return EFI_LOAD_ERROR;
            }
            continue;
        }
        if (entry->d_tag == DT_NULL) {
            found_null = TRUE;
            continue;
        }
        if (unsupported_dynamic_tag(entry->d_tag)) {
            return EFI_UNSUPPORTED;
        }
        switch (entry->d_tag) {
        case DT_RELA:
            if (have_relocation_address) {
                return EFI_LOAD_ERROR;
            }
            relocation_address = entry->d_un.d_ptr;
            have_relocation_address = TRUE;
            break;
        case DT_RELASZ:
            if (have_relocation_size) {
                return EFI_LOAD_ERROR;
            }
            relocation_size = entry->d_un.d_val;
            have_relocation_size = TRUE;
            break;
        case DT_RELAENT:
            if (have_relocation_entry_size) {
                return EFI_LOAD_ERROR;
            }
            relocation_entry_size = entry->d_un.d_val;
            have_relocation_entry_size = TRUE;
            break;
        case (Elf64_Sxword)DT_RELACOUNT:
            if (have_relative_count) {
                return EFI_LOAD_ERROR;
            }
            advertised_relative_count = entry->d_un.d_val;
            have_relative_count = TRUE;
            break;
        default:
            break;
        }
    }

    if (!found_null ||
        !have_relocation_address ||
        !have_relocation_size ||
        !have_relocation_entry_size ||
        relocation_size == 0 ||
        relocation_entry_size != sizeof(Elf64_Rela) ||
        relocation_size % sizeof(Elf64_Rela) != 0) {
        return EFI_LOAD_ERROR;
    }
    relocation_count = relocation_size / sizeof(Elf64_Rela);
    if (relocation_count > MAX_KERNEL_RELOCATIONS ||
        (have_relative_count &&
         advertised_relative_count != relocation_count) ||
        !virtual_range_to_file(
            programs,
            program_count,
            relocation_address,
            relocation_size,
            file_size,
            &relocation_file_offset) ||
        (relocation_file_offset & (sizeof(UINT64) - 1)) != 0) {
        return EFI_LOAD_ERROR;
    }

    relocations = (const Elf64_Rela*)(
        (const UINT8*)file + relocation_file_offset);

    /*
     * Validate the complete immutable table before writing a single target.
     * Requiring sorted, non-overlapping targets also rejects duplicate or
     * partially overlapping 64-bit fixups.
     */
    for (index = 0; index < (UINTN)relocation_count; ++index) {
        const Elf64_Rela* relocation = &relocations[index];
        UINT64 addend;
        if (ELF64_R_TYPE(relocation->r_info) != R_X86_64_RELATIVE ||
            ELF64_R_SYM(relocation->r_info) != 0 ||
            relocation->r_addend < 0 ||
            (relocation->r_offset & (sizeof(UINT64) - 1)) != 0 ||
            (index != 0 && relocation->r_offset < previous_target_end) ||
            !virtual_range_in_load(
                programs,
                program_count,
                relocation->r_offset,
                sizeof(UINT64),
                PF_R | PF_W,
                PF_X)) {
            return EFI_UNSUPPORTED;
        }
        addend = (UINT64)relocation->r_addend;
        if (!virtual_range_in_load(
                programs,
                program_count,
                addend,
                1,
                PF_R,
                0) ||
            relocation->r_offset > UINT64_MAX - sizeof(UINT64) ||
            image_base > UINT64_MAX - addend) {
            return EFI_LOAD_ERROR;
        }
        previous_target_end =
            relocation->r_offset + sizeof(UINT64);
    }

    if (image_base > UINT64_MAX - image_size) {
        return EFI_LOAD_ERROR;
    }
    for (index = 0; index < (UINTN)relocation_count; ++index) {
        const Elf64_Rela* relocation = &relocations[index];
        UINT64* target = (UINT64*)(UINTN)(
            image_base + relocation->r_offset);
        *target = image_base + (UINT64)relocation->r_addend;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS load_file(EFI_BOOT_SERVICES* services,
                            EFI_HANDLE image_handle,
                            const CHAR16* path,
                            VOID** output,
                            UINTN* output_size,
                            UINTN* output_pages) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL* loaded_image = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* filesystem = NULL;
    EFI_FILE_PROTOCOL* root = NULL;
    EFI_FILE_PROTOCOL* file = NULL;
    UINT64 file_size = 0;
    if (!services || !services->AllocatePages || !services->FreePages ||
        !path || !output || !output_size || !output_pages) {
        return EFI_INVALID_PARAMETER;
    }
    *output = NULL;
    *output_size = 0;
    *output_pages = 0;

    status = services->HandleProtocol(
        image_handle, &EFI_LOADED_IMAGE_PROTOCOL_GUID,
        (VOID**)&loaded_image);
    if (EFI_ERROR(status) || !loaded_image) {
        return EFI_ERROR(status) ? status : EFI_NOT_FOUND;
    }
    status = services->HandleProtocol(
        loaded_image->DeviceHandle, &EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,
        (VOID**)&filesystem);
    if (EFI_ERROR(status) || !filesystem || !filesystem->OpenVolume) {
        return EFI_ERROR(status) ? status : EFI_NOT_FOUND;
    }
    status = filesystem->OpenVolume(filesystem, &root);
    if (EFI_ERROR(status) || !root || !root->Open) {
        return EFI_ERROR(status) ? status : EFI_NOT_FOUND;
    }
    status = root->Open(
        root, &file, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status) || !file) {
        root->Close(root);
        return EFI_ERROR(status) ? status : EFI_NOT_FOUND;
    }

    status = file->SetPosition(file, UINT64_MAX);
    if (!EFI_ERROR(status)) {
        status = file->GetPosition(file, &file_size);
    }
    if (!EFI_ERROR(status)) {
        status = file->SetPosition(file, 0);
    }
    if (EFI_ERROR(status) || file_size == 0 || file_size > SIZE_MAX ||
        file_size > UINT64_MAX - PAGE_MASK) {
        file->Close(file);
        root->Close(root);
        return EFI_ERROR(status) ? status : EFI_LOAD_ERROR;
    }

    *output_size = (UINTN)file_size;
    *output_pages = (UINTN)((file_size + PAGE_MASK) /
                            KUROGANE_PAGE_SIZE);
    {
        EFI_PHYSICAL_ADDRESS address = UINT64_MAX;
        status = services->AllocatePages(
            AllocateMaxAddress, EfiLoaderData, *output_pages, &address);
        if (!EFI_ERROR(status)) {
            *output = (VOID*)(UINTN)address;
        }
    }
    if (!EFI_ERROR(status)) {
        UINTN read_size = *output_size;
        status = file->Read(file, &read_size, *output);
        if (EFI_ERROR(status) || read_size != *output_size) {
            services->FreePages(
                (EFI_PHYSICAL_ADDRESS)(UINTN)*output, *output_pages);
            *output = NULL;
            *output_size = 0;
            *output_pages = 0;
            if (!EFI_ERROR(status)) {
                status = EFI_LOAD_ERROR;
            }
        }
    }

    file->Close(file);
    root->Close(root);
    return status;
}

static EFI_STATUS load_kernel(EFI_BOOT_SERVICES* services,
                              const VOID* file,
                              UINTN file_size,
                              LoadedKernel* output) {
    const Elf64_Ehdr* header;
    const Elf64_Phdr* programs;
    const Elf64_Phdr* dynamic_program = NULL;
    EFI_PHYSICAL_ADDRESS image_base = 0;
    UINT64 image_start = UINT64_MAX;
    EFI_PHYSICAL_ADDRESS image_end = 0;
    BOOLEAN executable_entry = FALSE;
    BOOLEAN file_headers_loaded = FALSE;
    UINTN load_segments = 0;
    UINTN pages;
    UINTN index;

    if (!services || !file || !output ||
        file_size < sizeof(Elf64_Ehdr)) {
        return EFI_INVALID_PARAMETER;
    }
    memset(output, 0, sizeof(*output));
    header = (const Elf64_Ehdr*)file;
    if (header->e_ident[0] != 0x7F || header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' || header->e_ident[3] != 'F' ||
        header->e_ident[4] != ELFCLASS64 ||
        header->e_ident[5] != ELFDATA2LSB ||
        header->e_ident[6] != EV_CURRENT ||
        header->e_type != ET_DYN || header->e_machine != EM_X86_64 ||
        header->e_version != EV_CURRENT ||
        header->e_ehsize != sizeof(Elf64_Ehdr) ||
        header->e_phentsize != sizeof(Elf64_Phdr) ||
        header->e_phnum == 0 ||
        header->e_phnum > MAX_KERNEL_PROGRAM_HEADERS ||
        !range_inside(
            header->e_phoff,
            (UINT64)header->e_phnum * sizeof(Elf64_Phdr),
            file_size)) {
        return EFI_LOAD_ERROR;
    }
    programs = (const Elf64_Phdr*)((const UINT8*)file + header->e_phoff);

    for (index = 0; index < header->e_phnum; ++index) {
        const Elf64_Phdr* program = &programs[index];
        UINT64 segment_end;
        UINTN previous_index;

        if (program->p_type == PT_DYNAMIC) {
            if (dynamic_program != NULL) {
                return EFI_LOAD_ERROR;
            }
            dynamic_program = program;
            continue;
        }
        if (program->p_type != PT_LOAD || program->p_memsz == 0) {
            continue;
        }
        if (program->p_vaddr != program->p_paddr ||
            program->p_filesz > program->p_memsz ||
            !range_inside(program->p_offset, program->p_filesz, file_size) ||
            program->p_vaddr > UINT64_MAX - program->p_memsz ||
            program->p_align != KUROGANE_PAGE_SIZE ||
            (program->p_vaddr & PAGE_MASK) !=
                (program->p_offset & PAGE_MASK) ||
            (program->p_flags & PF_R) == 0 ||
            (program->p_flags & ~(PF_R | PF_W | PF_X)) != 0 ||
            (program->p_flags & (PF_W | PF_X)) == (PF_W | PF_X)) {
            return EFI_LOAD_ERROR;
        }
        segment_end = program->p_vaddr + program->p_memsz;
        if (segment_end > UINT64_MAX - PAGE_MASK) {
            return EFI_LOAD_ERROR;
        }
        if ((program->p_vaddr & ~PAGE_MASK) < image_start) {
            image_start = program->p_vaddr & ~PAGE_MASK;
        }
        segment_end = (segment_end + PAGE_MASK) & ~PAGE_MASK;
        if (segment_end > image_end) {
            image_end = segment_end;
        }
        for (previous_index = 0; previous_index < index;
             ++previous_index) {
            const Elf64_Phdr* previous = &programs[previous_index];
            UINT64 previous_end;
            if (previous->p_type != PT_LOAD ||
                previous->p_memsz == 0) {
                continue;
            }
            previous_end = previous->p_vaddr + previous->p_memsz;
            if (program->p_vaddr < previous_end &&
                previous->p_vaddr <
                    program->p_vaddr + program->p_memsz) {
                return EFI_LOAD_ERROR;
            }
        }
        if ((program->p_flags & PF_X) &&
            header->e_entry >= program->p_vaddr &&
            header->e_entry < program->p_vaddr + program->p_memsz) {
            executable_entry = TRUE;
        }
        if (program->p_vaddr == 0 &&
            program->p_offset == 0 &&
            program->p_filesz >=
                header->e_phoff +
                    (UINT64)header->e_phnum * sizeof(Elf64_Phdr)) {
            file_headers_loaded = TRUE;
        }
        ++load_segments;
    }

    if (load_segments == 0 ||
        !executable_entry ||
        !file_headers_loaded ||
        !dynamic_program ||
        image_start != 0 ||
        image_end == 0 ||
        image_end > SIZE_MAX ||
        dynamic_program->p_filesz == 0 ||
        dynamic_program->p_filesz != dynamic_program->p_memsz ||
        dynamic_program->p_filesz % sizeof(Elf64_Dyn) != 0 ||
        dynamic_program->p_align != sizeof(UINT64) ||
        (dynamic_program->p_offset & (sizeof(UINT64) - 1)) != 0 ||
        (dynamic_program->p_vaddr & (sizeof(UINT64) - 1)) != 0 ||
        (dynamic_program->p_flags & (PF_R | PF_W)) != (PF_R | PF_W) ||
        (dynamic_program->p_flags & PF_X) != 0) {
        return EFI_LOAD_ERROR;
    }

    {
        UINT64 mapped_dynamic_offset;
        if (!virtual_range_to_file(
                programs,
                header->e_phnum,
                dynamic_program->p_vaddr,
                dynamic_program->p_filesz,
                file_size,
                &mapped_dynamic_offset) ||
            mapped_dynamic_offset != dynamic_program->p_offset ||
            !virtual_range_in_load(
                programs,
                header->e_phnum,
                dynamic_program->p_vaddr,
                dynamic_program->p_filesz,
                PF_R | PF_W,
                PF_X)) {
            return EFI_LOAD_ERROR;
        }
    }

    pages = (UINTN)(image_end / KUROGANE_PAGE_SIZE);
    if (pages == 0) {
        return EFI_LOAD_ERROR;
    }
    {
        EFI_STATUS status = services->AllocatePages(
            AllocateAnyPages, EfiLoaderCode, pages, &image_base);
        if (EFI_ERROR(status)) {
            return status;
        }
        if (image_base > UINT64_MAX - image_end ||
            image_base > SIZE_MAX ||
            image_end > SIZE_MAX - (UINTN)image_base) {
            services->FreePages(image_base, pages);
            return EFI_OUT_OF_RESOURCES;
        }
        memset((VOID*)(UINTN)image_base, 0,
               pages * (UINTN)KUROGANE_PAGE_SIZE);
    }

    for (index = 0; index < header->e_phnum; ++index) {
        const Elf64_Phdr* program = &programs[index];
        if (program->p_type == PT_LOAD && program->p_filesz != 0) {
            memcpy((VOID*)(UINTN)(image_base + program->p_vaddr),
                   (const UINT8*)file + program->p_offset,
                   (UINTN)program->p_filesz);
        }
    }

    {
        EFI_STATUS status = apply_dynamic_relocations(
            file,
            file_size,
            programs,
            header->e_phnum,
            dynamic_program,
            image_base,
            image_end);
        if (EFI_ERROR(status)) {
            services->FreePages(image_base, pages);
            return status;
        }
    }

    output->entry = image_base + header->e_entry;
    output->start = image_base;
    output->end = image_base + image_end;
    return EFI_SUCCESS;
}

static BOOLEAN prepare_framebuffer(
    EFI_GRAPHICS_OUTPUT_PROTOCOL* graphics,
    KuroganeFramebuffer* output) {
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* information;
    UINT64 pitch;
    UINT64 required;

    if (!graphics || !graphics->Mode || !graphics->Mode->Info || !output ||
        graphics->Mode->SizeOfInfo <
            sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION)) {
        return FALSE;
    }
    information = graphics->Mode->Info;
    if (information->HorizontalResolution == 0 ||
        information->VerticalResolution == 0 ||
        information->PixelsPerScanLine <
            information->HorizontalResolution ||
        information->PixelsPerScanLine > UINT32_MAX / 4) {
        return FALSE;
    }
    pitch = (UINT64)information->PixelsPerScanLine * 4;
    if (information->VerticalResolution > UINT64_MAX / pitch) {
        return FALSE;
    }
    required = pitch * information->VerticalResolution;
    if (graphics->Mode->FrameBufferBase == 0 ||
        graphics->Mode->FrameBufferBase > UINT64_MAX - required ||
        required > graphics->Mode->FrameBufferSize) {
        return FALSE;
    }

    if (information->PixelFormat ==
        PixelBlueGreenRedReserved8BitPerColor) {
        output->pixel_format = KUROGANE_PIXEL_BGRX8;
    } else if (information->PixelFormat ==
               PixelRedGreenBlueReserved8BitPerColor) {
        output->pixel_format = KUROGANE_PIXEL_RGBX8;
    } else {
        return FALSE;
    }
    output->base = (UINT32*)(UINTN)graphics->Mode->FrameBufferBase;
    output->width = information->HorizontalResolution;
    output->height = information->VerticalResolution;
    output->pitch = (UINT32)pitch;
    output->bpp = 32;
    return TRUE;
}

static UINT32 normalize_memory_type(UINT32 type) {
    switch ((EFI_MEMORY_TYPE)type) {
    case EfiConventionalMemory:
        return KUROGANE_MEMORY_USABLE;
    case EfiACPIReclaimMemory:
        return KUROGANE_MEMORY_ACPI_RECLAIMABLE;
    case EfiACPIMemoryNVS:
        return KUROGANE_MEMORY_ACPI_NVS;
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:
        return KUROGANE_MEMORY_MMIO;
    default:
        return KUROGANE_MEMORY_RESERVED;
    }
}

static EFI_STATUS allocate_map_buffers(
    EFI_BOOT_SERVICES* services,
    RawMemoryMap* raw,
    KuroganeMemoryRegion** regions,
    UINTN* region_capacity) {
    EFI_STATUS status;
    UINTN required = 0;
    UINTN key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;
    UINTN slack;

    memset(raw, 0, sizeof(*raw));
    *regions = NULL;
    *region_capacity = 0;
    status = services->GetMemoryMap(
        &required, NULL, &key, &descriptor_size, &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL ||
        descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR) ||
        descriptor_size > MAX_DESCRIPTOR_SIZE ||
        descriptor_size > SIZE_MAX / MAP_SLACK_DESCRIPTORS) {
        return EFI_LOAD_ERROR;
    }
    slack = descriptor_size * MAP_SLACK_DESCRIPTORS;
    if (required > SIZE_MAX - slack) {
        return EFI_OUT_OF_RESOURCES;
    }
    raw->capacity = required + slack;
    raw->descriptor_size = descriptor_size;
    raw->descriptor_version = descriptor_version;
    status = services->AllocatePool(
        EfiLoaderData, raw->capacity, (VOID**)&raw->data);
    if (EFI_ERROR(status)) {
        return status;
    }

    *region_capacity =
        raw->capacity / descriptor_size + MAP_SLACK_DESCRIPTORS;
    if (*region_capacity > KUROGANE_MAX_MEMORY_REGIONS ||
        *region_capacity > SIZE_MAX / sizeof(KuroganeMemoryRegion)) {
        services->FreePool(raw->data);
        raw->data = NULL;
        return EFI_OUT_OF_RESOURCES;
    }
    status = services->AllocatePool(
        EfiLoaderData,
        *region_capacity * sizeof(KuroganeMemoryRegion),
        (VOID**)regions);
    if (EFI_ERROR(status)) {
        services->FreePool(raw->data);
        raw->data = NULL;
        return status;
    }
    memset(*regions, 0,
           *region_capacity * sizeof(KuroganeMemoryRegion));
    return EFI_SUCCESS;
}

static BOOLEAN normalize_memory_map(
    const RawMemoryMap* raw,
    KuroganeMemoryRegion* regions,
    UINTN capacity,
    UINTN* output_count) {
    UINTN offset = 0;
    UINTN count = 0;
    if (!raw || !raw->data || !regions || !output_count ||
        raw->descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR) ||
        raw->descriptor_size > MAX_DESCRIPTOR_SIZE ||
        raw->size > raw->capacity ||
        raw->size % raw->descriptor_size != 0) {
        return FALSE;
    }
    while (offset < raw->size) {
        const EFI_MEMORY_DESCRIPTOR* descriptor =
            (const EFI_MEMORY_DESCRIPTOR*)((const UINT8*)raw->data + offset);
        if (descriptor->NumberOfPages != 0) {
            if (count >= capacity ||
                (descriptor->PhysicalStart & PAGE_MASK) != 0 ||
                descriptor->NumberOfPages >
                    (UINT64_MAX - descriptor->PhysicalStart) /
                        KUROGANE_PAGE_SIZE) {
                return FALSE;
            }
            regions[count].physical_start = descriptor->PhysicalStart;
            regions[count].page_count = descriptor->NumberOfPages;
            regions[count].type = normalize_memory_type(descriptor->Type);
            regions[count].reserved = 0;
            regions[count].attributes = descriptor->Attribute;
            ++count;
        }
        offset += raw->descriptor_size;
    }
    *output_count = count;
    return count != 0;
}

static EFI_STATUS exit_boot_services(
    EFI_BOOT_SERVICES* services,
    EFI_HANDLE image_handle,
    KuroganeBootInfo* boot_info) {
    UINTN allocation_attempt;
    for (allocation_attempt = 0; allocation_attempt < 8;
         ++allocation_attempt) {
        RawMemoryMap raw;
        KuroganeMemoryRegion* regions = NULL;
        UINTN region_capacity = 0;
        UINTN exit_attempt;
        EFI_STATUS status = allocate_map_buffers(
            services, &raw, &regions, &region_capacity);
        if (EFI_ERROR(status)) {
            return status;
        }

        for (exit_attempt = 0; exit_attempt < 8; ++exit_attempt) {
            UINTN count;
            raw.size = raw.capacity;
            status = services->GetMemoryMap(
                &raw.size, raw.data, &raw.key,
                &raw.descriptor_size, &raw.descriptor_version);
            if (status == EFI_BUFFER_TOO_SMALL) {
                break;
            }
            if (EFI_ERROR(status) ||
                raw.descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR) ||
                raw.descriptor_size > MAX_DESCRIPTOR_SIZE) {
                services->FreePool(regions);
                services->FreePool(raw.data);
                return EFI_ERROR(status) ? status : EFI_LOAD_ERROR;
            }
            if (!normalize_memory_map(
                    &raw, regions, region_capacity, &count)) {
                services->FreePool(regions);
                services->FreePool(raw.data);
                return EFI_LOAD_ERROR;
            }
            boot_info->memory_regions = regions;
            boot_info->memory_region_count = count;
            status = services->ExitBootServices(image_handle, raw.key);
            if (!EFI_ERROR(status)) {
                return EFI_SUCCESS;
            }
            if (status != EFI_INVALID_PARAMETER) {
                services->FreePool(regions);
                services->FreePool(raw.data);
                return status;
            }
        }

        services->FreePool(regions);
        services->FreePool(raw.data);
    }
    return EFI_OUT_OF_RESOURCES;
}

static UINT64 find_rsdp(EFI_SYSTEM_TABLE* system_table) {
    UINTN index;
    UINT64 fallback = 0;
    if (!system_table || !system_table->ConfigurationTable) {
        return 0;
    }
    for (index = 0; index < system_table->NumberOfTableEntries; ++index) {
        const EFI_CONFIGURATION_TABLE* table =
            &system_table->ConfigurationTable[index];
        if (guid_equal(&table->VendorGuid, &EFI_ACPI_20_TABLE_GUID)) {
            return (UINT64)(UINTN)table->VendorTable;
        }
        if (guid_equal(&table->VendorGuid, &EFI_ACPI_TABLE_GUID)) {
            fallback = (UINT64)(UINTN)table->VendorTable;
        }
    }
    return fallback;
}

static UINT64 request_boot_flags(EFI_SYSTEM_TABLE* system_table) {
    static const UINT16 scan_f8 = 0x12;
    UINTN attempt;
    if (!system_table || !system_table->BootServices ||
        !system_table->ConIn || !system_table->ConIn->ReadKeyStroke) {
        return 0;
    }

    console_write(
        system_table,
        (const CHAR16*)L"Default boot=console. Press D for "
                        L"boot=desktop (DESKTOP ALPHA), S or F8 for safe "
                        L"mode, X for diagnostics...\r\n");
    /* Keep the choice window long enough for a headless serial-file watcher
       to observe the prompt and inject a QEMU monitor key deterministically. */
    for (attempt = 0; attempt < 300; ++attempt) {
        EFI_INPUT_KEY key;
        EFI_STATUS status;
        key.ScanCode = 0;
        key.UnicodeChar = 0;
        status = system_table->ConIn->ReadKeyStroke(
            system_table->ConIn, &key);
        if (!EFI_ERROR(status)) {
            if (key.ScanCode == scan_f8 || key.UnicodeChar == 's' ||
                key.UnicodeChar == 'S') {
                console_write(
                    system_table,
                    (const CHAR16*)L"Safe mode requested\r\n");
                return KUROGANE_BOOT_FLAG_SAFE_MODE;
            }
            if (key.UnicodeChar == 'd' || key.UnicodeChar == 'D') {
                console_write(
                    system_table,
                    (const CHAR16*)L"boot=desktop (DESKTOP ALPHA) "
                                    L"requested\r\n");
                return KUROGANE_BOOT_FLAG_FORCE_DESKTOP;
            }
            if (key.UnicodeChar == 'x' || key.UnicodeChar == 'X') {
                console_write(
                    system_table,
                    (const CHAR16*)L"Diagnostics mode requested\r\n");
                return KUROGANE_BOOT_FLAG_SAFE_MODE |
                       KUROGANE_BOOT_FLAG_DIAGNOSTICS;
            }
        }
        if (system_table->BootServices->Stall) {
            system_table->BootServices->Stall(10000);
        }
    }
    return 0;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle,
                           EFI_SYSTEM_TABLE* system_table) {
    static const CHAR16 kernel_path[] = {
        '\\', 'k', 'e', 'r', 'n', 'e', 'l', '.', 'e', 'l', 'f', 0
    };
    static const CHAR16 installer_path[] = {
        '\\', 'i', 'n', 's', 't', 'a', 'l', 'l', '.', 'p', 'k', 'g', 0
    };
    EFI_BOOT_SERVICES* services;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* graphics = NULL;
    KuroganeBootInfo* boot_info = NULL;
    VOID* kernel_file = NULL;
    UINTN kernel_file_size = 0;
    UINTN kernel_file_pages = 0;
    VOID* installation_package = NULL;
    UINTN installation_package_size = 0;
    UINTN installation_package_pages = 0;
    UINT64 boot_flags;
    LoadedKernel kernel;
    EFI_STATUS status;

    if (!system_table || !system_table->BootServices) {
        return EFI_INVALID_PARAMETER;
    }
    services = system_table->BootServices;
    console_write(
        system_table,
        (const CHAR16*)L"KuroganeOS loader " KUROGANE_VERSION_WIDE L"\r\n");
    boot_flags = request_boot_flags(system_table);

    status = services->HandleProtocol(
        system_table->ConsoleOutHandle,
        &EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
        (VOID**)&graphics);
    if (EFI_ERROR(status) || !graphics) {
        graphics = NULL;
        status = services->LocateProtocol(
            &EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, NULL, (VOID**)&graphics);
    }
    if (EFI_ERROR(status) || !graphics) {
        console_write(system_table, (const CHAR16*)L"ERROR: GOP unavailable\r\n");
        return EFI_ERROR(status) ? status : EFI_NOT_FOUND;
    }

    status = load_file(
        services, image_handle, kernel_path, &kernel_file, &kernel_file_size,
        &kernel_file_pages);
    if (EFI_ERROR(status)) {
        console_write(system_table,
                      (const CHAR16*)L"ERROR: kernel.elf unavailable\r\n");
        return status;
    }

    status = load_file(
        services, image_handle, installer_path, &installation_package,
        &installation_package_size, &installation_package_pages);
    if (!EFI_ERROR(status)) {
        if (installation_package_size > 16U * 1024U * 1024U) {
            services->FreePages(
                (EFI_PHYSICAL_ADDRESS)(UINTN)installation_package,
                installation_package_pages);
            console_write(system_table,
                          (const CHAR16*)L"ERROR: install.pkg too large\r\n");
            return EFI_LOAD_ERROR;
        }
        boot_flags = KUROGANE_BOOT_FLAG_INSTALLER;
        console_write(system_table,
                      (const CHAR16*)L"Installer package loaded\r\n");
    } else if (status != EFI_NOT_FOUND) {
        console_write(system_table,
                      (const CHAR16*)L"ERROR: cannot load install.pkg\r\n");
        return status;
    }
    status = load_kernel(
        services, kernel_file, kernel_file_size, &kernel);
    services->FreePages(
        (EFI_PHYSICAL_ADDRESS)(UINTN)kernel_file, kernel_file_pages);
    if (EFI_ERROR(status)) {
        console_write(system_table,
                      (const CHAR16*)L"ERROR: invalid kernel ELF\r\n");
        return status;
    }

    status = services->AllocatePool(
        EfiLoaderData, sizeof(KuroganeBootInfo), (VOID**)&boot_info);
    if (EFI_ERROR(status)) {
        return status;
    }
    memset(boot_info, 0, sizeof(*boot_info));
    if (!prepare_framebuffer(graphics, &boot_info->framebuffer)) {
        console_write(system_table,
                      (const CHAR16*)L"ERROR: unsupported framebuffer\r\n");
        return EFI_UNSUPPORTED;
    }

    boot_info->magic = KUROGANE_BOOT_MAGIC;
    boot_info->version = KUROGANE_BOOT_PROTOCOL_VERSION;
    boot_info->size = sizeof(*boot_info);
    boot_info->rsdp_address = find_rsdp(system_table);
    boot_info->kernel_physical_start = kernel.start;
    boot_info->kernel_physical_end = kernel.end;
    boot_info->flags = boot_flags;
    boot_info->installation_package = installation_package;
    boot_info->installation_package_size = installation_package_size;
    console_write(system_table,
                  (const CHAR16*)L"Exiting boot services...\r\n");

    status = exit_boot_services(services, image_handle, boot_info);
    if (EFI_ERROR(status)) {
        return status;
    }

    ((KuroganeKernelEntry)(UINTN)kernel.entry)(boot_info);
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
