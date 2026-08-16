#include "installer.hpp"

#include "disk_layout.hpp"
#include "package.hpp"
#include "../drivers/keyboard.hpp"
#include "../fs/fat32.hpp"
#include "../libk/crc.hpp"
#include "../storage/ahci.hpp"
#include "../storage/gpt.hpp"
#include "../storage/partition_device.hpp"
#include "../terminal.hpp"

namespace install::installer {
namespace {

storage::partition::Device g_esp_partition{};
storage::partition::Device g_root_partition{};
fs::fat32::FileSystem g_esp{};
fs::fat32::FileSystem g_root{};
uint8_t g_verify_buffer[4096]{};

[[noreturn]] void halt_forever() {
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

[[noreturn]] void fail(const char* reason) {
    terminal::write("installer error: ");
    terminal::println(reason);
    terminal::println("[TEST] installer_complete: FAIL");
    halt_forever();
}

bool strings_equal(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) return false;
    size_t index = 0U;
    while (left[index] == right[index] && left[index] != '\0') ++index;
    return left[index] == right[index];
}

size_t read_line(char* output, size_t capacity) {
    if (output == nullptr || capacity < 2U) return 0U;
    size_t length = 0U;
    output[0] = '\0';
    for (;;) {
        drivers::keyboard::poll();
        char character = 0;
        if (!drivers::keyboard::try_read_char(character)) {
            __asm__ volatile("pause");
            continue;
        }
        if (character == '\r' || character == '\n') {
            terminal::println();
            output[length] = '\0';
            return length;
        }
        if (character == '\b') {
            if (length != 0U) {
                --length;
                output[length] = '\0';
                terminal::backspace();
            }
            continue;
        }
        if (character >= 0x20 && character <= 0x7E &&
            length + 1U < capacity) {
            output[length++] = character;
            output[length] = '\0';
            terminal::put(character);
        }
    }
}

bool parse_index(const char* text, size_t count, size_t* output) {
    if (text == nullptr || output == nullptr || text[0] == '\0') return false;
    size_t value = 0U;
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        if (text[index] < '0' || text[index] > '9') return false;
        const size_t digit = static_cast<size_t>(text[index] - '0');
        if (value > (SIZE_MAX - digit) / 10U) return false;
        value = value * 10U + digit;
    }
    if (value >= count) return false;
    *output = value;
    return true;
}

bool make_directory(fs::fat32::FileSystem* filesystem, const char* path) {
    const fs::fat32::Status status = fs::fat32::mkdir(filesystem, path);
    return status == fs::fat32::Status::Ok ||
        status == fs::fat32::Status::AlreadyExists;
}

bool prepare_directories() {
    static const char* const esp_directories[] = {"/EFI", "/EFI/BOOT"};
    static const char* const root_directories[] = {
        "/boot", "/etc", "/apps", "/gui", "/system", "/var"
    };
    for (const char* path : esp_directories) {
        if (!make_directory(&g_esp, path)) return false;
    }
    for (const char* path : root_directories) {
        if (!make_directory(&g_root, path)) return false;
    }
    return true;
}

bool copy_file(const package::File& file) {
    fs::fat32::FileSystem* filesystem =
        file.destination == package::DESTINATION_ESP ? &g_esp : &g_root;
    if (fs::fat32::create(filesystem, file.path) != fs::fat32::Status::Ok) {
        return false;
    }
    if (file.size != 0U &&
        fs::fat32::write(
            filesystem, file.path, 0U, file.data, file.size) !=
            fs::fat32::Status::Ok) {
        return false;
    }
    return true;
}

bool verify_file(const package::File& file) {
    fs::fat32::FileSystem* filesystem =
        file.destination == package::DESTINATION_ESP ? &g_esp : &g_root;
    size_t offset = 0U;
    while (offset < file.size) {
        const size_t remaining = file.size - offset;
        const size_t chunk = remaining < sizeof(g_verify_buffer)
            ? remaining
            : sizeof(g_verify_buffer);
        size_t bytes_read = 0U;
        if (fs::fat32::read(
                filesystem, file.path, offset, g_verify_buffer, chunk,
                &bytes_read) != fs::fat32::Status::Ok ||
            bytes_read != chunk) {
            return false;
        }
        for (size_t index = 0U; index < chunk; ++index) {
            if (g_verify_buffer[index] != file.data[offset + index]) {
                return false;
            }
        }
        offset += chunk;
    }
    fs::fat32::Stat info{};
    return fs::fat32::stat(filesystem, file.path, &info) ==
               fs::fat32::Status::Ok &&
        info.type == fs::fat32::EntryType::File && info.size == file.size;
}

} // namespace

[[noreturn]] void run_interactive(
    const void* package_bytes,
    size_t package_size) {
    package::View payload{};
    const package::Status package_status =
        package::parse(package_bytes, package_size, &payload);
    if (package_status != package::Status::Ok) {
        fail(package::status_message(package_status));
    }
    terminal::println();
    terminal::println("KuroganeOS text installer 2.0");
    terminal::write("validated package files: ");
    terminal::write_u64(payload.file_count);
    terminal::println();
    terminal::println("installer: detected disks");

    const size_t disk_count = storage::ahci::device_count();
    if (disk_count == 0U) fail("no writable SATA disk detected");
    for (size_t index = 0U; index < disk_count; ++index) {
        const storage::ahci::DeviceInfo* info =
            storage::ahci::device_info_at(index);
        terminal::write("  [");
        terminal::write_u64(index);
        terminal::write("] ");
        terminal::write(info != nullptr ? info->model : "unknown SATA disk");
        terminal::write(" - ");
        terminal::write_u64(
            info != nullptr
                ? (info->sector_count * info->sector_size) / (1024U * 1024U)
                : 0U);
        terminal::println(" MiB");
    }

    char line[32]{};
    size_t target_index = SIZE_MAX;
    while (target_index == SIZE_MAX) {
        terminal::write("installer: select target disk index: ");
        read_line(line, sizeof(line));
        if (!parse_index(line, disk_count, &target_index)) {
            terminal::println("invalid disk index");
            target_index = SIZE_MAX;
        }
    }
    const storage::block::Device* target =
        storage::ahci::device_at(target_index);
    if (target == nullptr) fail("selected disk disappeared");

    terminal::println("WARNING: the selected guest disk will be repartitioned.");
    terminal::write("installer: type INSTALL to confirm: ");
    read_line(line, sizeof(line));
    if (!strings_equal(line, "INSTALL")) {
        terminal::println("installation cancelled; no disk write was attempted");
        terminal::println("[TEST] installer_cancel_safe: PASS");
        halt_forever();
    }

    terminal::println("installer stage 1/8: target confirmed");
    disk_layout::Layout layout{};
    const disk_layout::Status layout_status =
        disk_layout::prepare_empty_disk(target, &layout);
    if (layout_status != disk_layout::Status::Ok) {
        fail(disk_layout::status_message(layout_status));
    }
    terminal::println("installer stage 2/8: protective MBR and mirrored GPT written");

    storage::gpt::Table table{};
    if (storage::gpt::parse_primary(target, &table).status !=
        storage::gpt::Status::Ok || table.partition_count != 2U) {
        fail("written GPT did not validate");
    }
    if (storage::partition::initialize(
            &g_esp_partition, target, layout.esp_first_lba,
            layout.esp_sector_count) != storage::block::Status::Ok ||
        storage::partition::initialize(
            &g_root_partition, target, layout.root_first_lba,
            layout.root_sector_count) != storage::block::Status::Ok) {
        fail("partition views could not be created");
    }
    terminal::println("installer stage 3/8: ESP and root partitions selected");

    if (fs::fat32::format(
            storage::partition::as_block_device(&g_esp_partition),
            "KURO_ESP", 1U, static_cast<uint32_t>(layout.esp_first_lba)) !=
            fs::fat32::Status::Ok ||
        fs::fat32::format(
            storage::partition::as_block_device(&g_root_partition),
            "KURO_ROOT", 8U, static_cast<uint32_t>(layout.root_first_lba)) !=
            fs::fat32::Status::Ok) {
        fail("FAT32 formatting failed");
    }
    terminal::println("installer stage 4/8: FAT32 filesystems prepared");
    if (fs::fat32::mount(
            &g_esp, storage::partition::as_block_device(&g_esp_partition)) !=
            fs::fat32::Status::Ok ||
        fs::fat32::mount(
            &g_root, storage::partition::as_block_device(&g_root_partition)) !=
            fs::fat32::Status::Ok || !prepare_directories()) {
        fail("filesystem mount or directory creation failed");
    }
    terminal::println("installer stage 5/8: directory tree created");

    for (size_t index = 0U; index < payload.file_count; ++index) {
        package::File file{};
        if (package::file_at(payload, index, &file) != package::Status::Ok ||
            !copy_file(file)) {
            fail("package copy failed");
        }
    }
    terminal::println("installer stage 6/8: system and UEFI bootloader copied");

    static constexpr char kFirstRun[] = "pending\n";
    if (fs::fat32::create(&g_root, "/etc/first.run") !=
            fs::fat32::Status::Ok ||
        fs::fat32::write(
            &g_root, "/etc/first.run", 0U, kFirstRun,
            sizeof(kFirstRun) - 1U) != fs::fat32::Status::Ok ||
        fs::fat32::sync(&g_esp) != fs::fat32::Status::Ok ||
        fs::fat32::sync(&g_root) != fs::fat32::Status::Ok) {
        fail("boot configuration sync failed");
    }
    terminal::println("installer stage 7/8: boot configuration committed");

    for (size_t index = 0U; index < payload.file_count; ++index) {
        package::File file{};
        if (package::file_at(payload, index, &file) != package::Status::Ok ||
            !verify_file(file)) {
            fail("installed file verification failed");
        }
    }
    terminal::println("installer stage 8/8: installed files verified");
    terminal::println("[TEST] installer_gpt: PASS");
    terminal::println("[TEST] installer_filesystems: PASS");
    terminal::println("[TEST] installer_uefi_bootloader: PASS");
    terminal::println("[TEST] installer_complete: PASS");
    terminal::println("Installation complete. Power off and boot without the ISO.");
    halt_forever();
}

} // namespace install::installer
