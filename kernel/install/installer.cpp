#include "installer.hpp"

#include "disk_layout.hpp"
#include "fat32_reliable_file.hpp"
#include "package.hpp"
#include "../drivers/framebuffer.hpp"
#include "../drivers/keyboard.hpp"
#include "../fs/fat32.hpp"
#include "../fs/root_volume.hpp"
#include "../storage/ahci.hpp"
#include "../storage/gpt.hpp"
#include "../storage/partition_device.hpp"
#include "../terminal.hpp"
#include "../../common/version.h"

namespace install::installer {
namespace {

storage::partition::Device g_esp_partition{};
storage::partition::Device g_root_partition{};
fs::fat32::FileSystem g_esp{};
fs::fat32::FileSystem g_root{};
constexpr size_t kVerifyBufferBytes = 1024U * 1024U;
alignas(64) uint8_t g_verify_buffer[kVerifyBufferBytes]{};

constexpr graphics::Color kBackground = UINT32_C(0x050608);
constexpr graphics::Color kPanel = UINT32_C(0x111317);
constexpr graphics::Color kPanelRaised = UINT32_C(0x191C21);
constexpr graphics::Color kBorder = UINT32_C(0x343941);
constexpr graphics::Color kText = UINT32_C(0xECEEF1);
constexpr graphics::Color kMuted = UINT32_C(0x9098A3);
constexpr graphics::Color kAccent = UINT32_C(0xE0192D);
constexpr graphics::Color kAccentBright = UINT32_C(0xFF3347);
constexpr graphics::Color kDanger = UINT32_C(0xFF4055);

constexpr size_t kUsernameCapacity = 24U;
constexpr size_t kPasswordCapacity = 48U;

enum class Language : uint8_t {
    English = 0,
    Polish,
};

struct InstallProfile {
    Language language;
    char username[kUsernameCapacity];
    char password[kPasswordCapacity];
    bool password_required;
};

[[noreturn]] void halt_forever() {
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

void copy_text(char* output, size_t capacity, const char* input) {
    if (output == nullptr || capacity == 0U) return;
    size_t index = 0U;
    if (input != nullptr) {
        while (index + 1U < capacity && input[index] != '\0') {
            output[index] = input[index];
            ++index;
        }
    }
    output[index] = '\0';
}

size_t text_length(const char* value) {
    if (value == nullptr) return 0U;
    size_t length = 0U;
    while (value[length] != '\0') ++length;
    return length;
}

void append_text(char* output, size_t capacity, const char* input) {
    if (output == nullptr || input == nullptr || capacity == 0U) return;
    size_t used = text_length(output);
    size_t index = 0U;
    while (used + 1U < capacity && input[index] != '\0') {
        output[used++] = input[index++];
    }
    output[used] = '\0';
}

bool strings_equal(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) return false;
    size_t index = 0U;
    while (left[index] == right[index] && left[index] != '\0') ++index;
    return left[index] == right[index];
}

char ascii_upper(char value) {
    if (value >= 'a' && value <= 'z') {
        return static_cast<char>(value - 'a' + 'A');
    }
    return value;
}

void u64_to_decimal(uint64_t value, char* output, size_t capacity) {
    if (output == nullptr || capacity < 2U) return;
    char reversed[32]{};
    size_t count = 0U;
    do {
        reversed[count++] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(reversed));
    size_t written = 0U;
    while (count != 0U && written + 1U < capacity) {
        output[written++] = reversed[--count];
    }
    output[written] = '\0';
}

void u64_to_hex(uint64_t value, char output[17]) {
    static constexpr char digits[] = "0123456789ABCDEF";
    for (size_t index = 0U; index < 16U; ++index) {
        const unsigned shift = static_cast<unsigned>((15U - index) * 4U);
        output[index] = digits[(value >> shift) & UINT64_C(0xF)];
    }
    output[16] = '\0';
}

uint64_t credential_hash(const char* username, const char* password) {
    uint64_t hash = UINT64_C(1469598103934665603);
    static constexpr char domain[] = "KuroganeOS-3.3-dev:";
    for (char value : domain) {
        if (value == '\0') break;
        hash ^= static_cast<uint8_t>(value);
        hash *= UINT64_C(1099511628211);
    }
    const char* fields[] = {username, ":", password};
    for (const char* field : fields) {
        if (field == nullptr) continue;
        for (size_t index = 0U; field[index] != '\0'; ++index) {
            hash ^= static_cast<uint8_t>(field[index]);
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

void draw_brand_header(const char* section) {
    if (!graphics::available()) return;
    graphics::clear(kBackground);
    const int32_t width = static_cast<int32_t>(graphics::width());
    graphics::fill_rect(0, 0, width, 54, UINT32_C(0x090A0D));
    graphics::fill_rect(0, 52, width, 2, kAccent);
    graphics::draw_text(28, 18, "KUROGANEOS", kText, kBackground, 2U, true);
    graphics::draw_text(190, 20, KUROGANE_VERSION_STRING " DEV BETA",
                        kAccentBright, kBackground, 1U, true);
    if (section != nullptr) {
        graphics::draw_text(width - 230, 20, section,
                            kMuted, kBackground, 1U, true);
    }
}

void draw_footer(const char* text) {
    if (!graphics::available() || text == nullptr) return;
    const int32_t height = static_cast<int32_t>(graphics::height());
    graphics::draw_text(28, height - 34, text, kMuted, kBackground, 1U, true);
}

void draw_setup_title(const char* title, const char* subtitle) {
    draw_brand_header("RED FLUX SETUP");
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t panel_x = width / 2 - 300;
    graphics::fill_rect(panel_x, 100, 600, 390, kPanel);
    graphics::draw_rect(panel_x, 100, 600, 390, kBorder, 1U);
    graphics::fill_rect(panel_x, 100, 6, 390, kAccent);
    graphics::draw_text(panel_x + 34, 130, title, kText, kPanel, 2U, true);
    if (subtitle != nullptr) {
        graphics::draw_text(panel_x + 34, 166, subtitle, kMuted, kPanel, 1U, true);
    }
}

void draw_option(
    size_t position,
    const char* label,
    const char* description,
    bool selected) {
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t x = width / 2 - 250;
    const int32_t y = 220 + static_cast<int32_t>(position) * 92;
    graphics::fill_rect(x, y, 500, 70, selected ? kPanelRaised : kPanel);
    graphics::draw_rect(x, y, 500, 70, selected ? kAccent : kBorder,
                        selected ? 2U : 1U);
    if (selected) graphics::fill_rect(x, y, 5, 70, kAccent);
    graphics::draw_text(x + 24, y + 16, label,
                        selected ? kAccentBright : kText,
                        kPanel, 2U, true);
    if (description != nullptr) {
        graphics::draw_text(x + 24, y + 46, description,
                            kMuted, kPanel, 1U, true);
    }
}

drivers::keyboard::KeyEvent wait_key() {
    for (;;) {
        drivers::keyboard::poll();
        drivers::keyboard::KeyEvent event{};
        if (drivers::keyboard::try_read_event(event) && event.pressed) {
            return event;
        }
        __asm__ volatile("pause");
    }
}

size_t choose_two(
    const char* title,
    const char* subtitle,
    const char* first,
    const char* first_description,
    const char* second,
    const char* second_description,
    size_t initial = 0U) {
    size_t selected = initial > 1U ? 0U : initial;
    for (;;) {
        draw_setup_title(title, subtitle);
        draw_option(0U, first, first_description, selected == 0U);
        draw_option(1U, second, second_description, selected == 1U);
        draw_footer("ARROWS: SELECT   ENTER: CONTINUE");
        const auto event = wait_key();
        if (event.key == drivers::keyboard::KeyCode::ArrowUp ||
            event.key == drivers::keyboard::KeyCode::ArrowLeft) {
            selected = 0U;
        } else if (event.key == drivers::keyboard::KeyCode::ArrowDown ||
                   event.key == drivers::keyboard::KeyCode::ArrowRight ||
                   event.key == drivers::keyboard::KeyCode::Tab) {
            selected = 1U;
        } else if (event.key == drivers::keyboard::KeyCode::Enter ||
                   event.key == drivers::keyboard::KeyCode::KeypadEnter) {
            return selected;
        }
    }
}

void draw_input_page(
    const char* title,
    const char* subtitle,
    const char* value,
    bool masked,
    const char* error) {
    draw_setup_title(title, subtitle);
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t x = width / 2 - 250;
    graphics::fill_rect(x, 230, 500, 64, kPanelRaised);
    graphics::draw_rect(x, 230, 500, 64, kAccent, 2U);
    char display[64]{};
    if (masked) {
        const size_t length = text_length(value);
        const size_t count = length < sizeof(display) - 1U
            ? length : sizeof(display) - 1U;
        for (size_t index = 0U; index < count; ++index) display[index] = '*';
        display[count] = '\0';
    } else {
        copy_text(display, sizeof(display), value);
    }
    graphics::draw_text(x + 22, 252,
                        display[0] == '\0' ? "_" : display,
                        kText, kPanelRaised, 2U, true);
    if (error != nullptr) {
        graphics::draw_text(x, 320, error, kDanger, kPanel, 1U, true);
    }
    draw_footer("TYPE VALUE   BACKSPACE: ERASE   ENTER: CONTINUE   ESC: BACK");
}

bool valid_username(const char* value) {
    const size_t length = text_length(value);
    if (length == 0U || length >= kUsernameCapacity) return false;
    for (size_t index = 0U; index < length; ++index) {
        const char ch = value[index];
        const bool alpha = (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z');
        const bool digit = ch >= '0' && ch <= '9';
        if (!alpha && !digit && ch != '_' && ch != '-') return false;
    }
    return true;
}

bool read_input(
    const char* title,
    const char* subtitle,
    char* output,
    size_t capacity,
    bool masked,
    bool username_rules) {
    if (output == nullptr || capacity < 2U) return false;
    size_t length = text_length(output);
    const char* error = nullptr;
    for (;;) {
        draw_input_page(title, subtitle, output, masked, error);
        const auto event = wait_key();
        error = nullptr;
        if (event.key == drivers::keyboard::KeyCode::Escape) return false;
        if (event.key == drivers::keyboard::KeyCode::Backspace) {
            if (length != 0U) output[--length] = '\0';
            continue;
        }
        if (event.key == drivers::keyboard::KeyCode::Enter ||
            event.key == drivers::keyboard::KeyCode::KeypadEnter) {
            if (length == 0U) {
                error = "VALUE CANNOT BE EMPTY";
                continue;
            }
            if (username_rules && !valid_username(output)) {
                error = "USE LETTERS, DIGITS, _ OR - ONLY";
                continue;
            }
            return true;
        }
        const char ch = event.character;
        if (ch >= 0x20 && ch <= 0x7E && length + 1U < capacity) {
            output[length++] = ch;
            output[length] = '\0';
        }
    }
}

void show_error(const char* reason) {
    draw_brand_header("SETUP ERROR");
    const int32_t width = static_cast<int32_t>(graphics::width());
    graphics::fill_rect(width / 2 - 300, 180, 600, 220, kPanel);
    graphics::draw_rect(width / 2 - 300, 180, 600, 220, kDanger, 2U);
    graphics::draw_text(width / 2 - 260, 220, "INSTALLATION STOPPED",
                        kDanger, kPanel, 2U, true);
    graphics::draw_text(width / 2 - 260, 270, reason,
                        kText, kPanel, 1U, true);
    graphics::draw_text(width / 2 - 260, 330,
                        "REBOOT AND RETRY ON THE SAME DISK",
                        kMuted, kPanel, 1U, true);
}

[[noreturn]] void fail(const char* reason) {
    terminal::write("installer error: ");
    terminal::println(reason);
    terminal::println("[TEST] installer_complete: FAIL");
    show_error(reason);
    halt_forever();
}

void report_fs_failure(
    size_t package_index,
    const package::File& file,
    const char* operation,
    fs::fat32::Status status) {
    terminal::write("[INSTALL][COPY] file=");
    terminal::write_u64(package_index + 1U);
    terminal::write(" destination=");
    terminal::write(
        file.destination == package::DESTINATION_ESP ? "ESP" : "ROOT");
    terminal::write(" path=");
    terminal::write(file.path);
    terminal::write(" operation=");
    terminal::write(operation);
    terminal::write(" status=");
    terminal::println(fs::fat32::status_message(status));
}

fs::fat32::Status ensure_parent_directories(
    fs::fat32::FileSystem* filesystem,
    const char* path) {
    if (filesystem == nullptr || path == nullptr || path[0] != '/') {
        return fs::fat32::Status::InvalidPath;
    }
    const size_t length = text_length(path);
    if (length == 0U || length > fs::fat32::MAX_PATH_LENGTH) {
        return fs::fat32::Status::PathTooLong;
    }

    char prefix[fs::fat32::MAX_PATH_LENGTH + 1U]{};
    for (size_t index = 0U; index < length; ++index) {
        prefix[index] = path[index];
        if (path[index] != '/' || index == 0U) continue;
        prefix[index] = '\0';
        const fs::fat32::Status status = fs::fat32::mkdir(filesystem, prefix);
        if (status != fs::fat32::Status::Ok &&
            status != fs::fat32::Status::AlreadyExists) {
            return status;
        }
        prefix[index] = '/';
    }
    return fs::fat32::Status::Ok;
}

bool ensure_root_layout() {
    // Empty directories are part of the installed filesystem contract and
    // cannot be inferred from a file-only install package. Keep that contract
    // explicit here; package-specific parent directories remain data-driven.
    static constexpr const char* kDirectories[] = {
        "/bin",
        "/boot",
        "/dev",
        "/etc",
        "/home",
        "/proc",
        "/system",
        "/system/bin",
        "/tmp",
        "/var",
        "/var/log",
    };

    for (const char* directory : kDirectories) {
        const fs::fat32::Status status = fs::fat32::mkdir(&g_root, directory);
        if (status == fs::fat32::Status::Ok ||
            status == fs::fat32::Status::AlreadyExists) {
            continue;
        }
        terminal::write("[INSTALL][LAYOUT] mkdir path=");
        terminal::write(directory);
        terminal::write(" status=");
        terminal::println(fs::fat32::status_message(status));
        return false;
    }
    return true;
}

bool deploy_file(const package::File& file, size_t package_index) {
    fs::fat32::FileSystem* filesystem =
        file.destination == package::DESTINATION_ESP ? &g_esp : &g_root;

    fs::fat32::Status status = ensure_parent_directories(filesystem, file.path);
    if (status != fs::fat32::Status::Ok) {
        report_fs_failure(package_index, file, "mkdir-parent", status);
        return false;
    }

    status = fs::fat32::unlink(filesystem, file.path);
    if (status != fs::fat32::Status::Ok &&
        status != fs::fat32::Status::NotFound) {
        report_fs_failure(package_index, file, "remove-stale", status);
        return false;
    }

    status = fs::fat32::create(filesystem, file.path);
    if (status != fs::fat32::Status::Ok) {
        report_fs_failure(package_index, file, "create", status);
        return false;
    }
    if (file.size != 0U) {
        status = fs::fat32::write(
            filesystem, file.path, 0U, file.data, file.size);
        if (status != fs::fat32::Status::Ok) {
            report_fs_failure(package_index, file, "write", status);
            return false;
        }
    }
    return true;
}

bool deploy_destination(const package::View& payload, uint32_t destination) {
    for (size_t index = 0U; index < payload.file_count; ++index) {
        package::File file{};
        const package::Status package_status =
            package::file_at(payload, index, &file);
        if (package_status != package::Status::Ok) {
            terminal::write("[INSTALL][PACKAGE] file=");
            terminal::write_u64(index + 1U);
            terminal::write(" status=");
            terminal::println(package::status_message(package_status));
            return false;
        }
        if (file.destination == destination && !deploy_file(file, index)) {
            return false;
        }
    }
    return true;
}

bool verify_file(const package::File& file, size_t package_index) {
    fs::fat32::FileSystem* filesystem =
        file.destination == package::DESTINATION_ESP ? &g_esp : &g_root;
    size_t offset = 0U;
    while (offset < file.size) {
        const size_t remaining = file.size - offset;
        const size_t chunk = remaining < sizeof(g_verify_buffer)
            ? remaining : sizeof(g_verify_buffer);
        size_t bytes_read = 0U;
        const fs::fat32::Status status = fs::fat32::read(
            filesystem, file.path, offset, g_verify_buffer, chunk, &bytes_read);
        if (status != fs::fat32::Status::Ok || bytes_read != chunk) {
            report_fs_failure(package_index, file, "verify-read", status);
            return false;
        }
        for (size_t index = 0U; index < chunk; ++index) {
            if (g_verify_buffer[index] != file.data[offset + index]) {
                terminal::write("[INSTALL][VERIFY] byte mismatch path=");
                terminal::println(file.path);
                return false;
            }
        }
        offset += chunk;
    }
    fs::fat32::Stat info{};
    const fs::fat32::Status stat_status =
        fs::fat32::stat(filesystem, file.path, &info);
    if (stat_status != fs::fat32::Status::Ok ||
        info.type != fs::fat32::EntryType::File || info.size != file.size) {
        report_fs_failure(package_index, file, "verify-stat", stat_status);
        return false;
    }
    return true;
}

bool verify_destination(const package::View& payload, uint32_t destination) {
    for (size_t index = 0U; index < payload.file_count; ++index) {
        package::File file{};
        const package::Status package_status =
            package::file_at(payload, index, &file);
        if (package_status != package::Status::Ok) {
            terminal::write("[INSTALL][VERIFY] package file=");
            terminal::write_u64(index + 1U);
            terminal::write(" status=");
            terminal::println(package::status_message(package_status));
            return false;
        }
        if (file.destination != destination) continue;

        terminal::write("[INSTALL][VERIFY] file=");
        terminal::write_u64(index + 1U);
        terminal::write("/");
        terminal::write_u64(payload.file_count);
        terminal::write(" destination=");
        terminal::write(
            destination == package::DESTINATION_ESP ? "ESP" : "ROOT");
        terminal::write(" path=");
        terminal::write(file.path);
        terminal::write(" bytes=");
        terminal::write_u64(file.size);
        terminal::println();

        if (!verify_file(file, index)) {
            return false;
        }
        terminal::write("[INSTALL][VERIFY] PASS path=");
        terminal::println(file.path);
    }
    return true;
}

bool reliable_state_paths(
    const char* path,
    ::install::reliable_file::Paths* output) {
    if (path == nullptr || output == nullptr) return false;
    if (strings_equal(path, "/etc/locale.cfg")) {
        *output = {
            path,
            "/etc/locale.new",
            "/etc/locale.bak",
            "/etc/locale.old",
        };
        return true;
    }
    if (strings_equal(path, "/etc/user.cfg")) {
        *output = {
            path,
            "/etc/user.new",
            "/etc/user.bak",
            "/etc/user.old",
        };
        return true;
    }
    if (strings_equal(path, "/etc/first.run")) {
        *output = {
            path,
            "/etc/first.new",
            "/etc/first.bak",
            "/etc/first.old",
        };
        return true;
    }
    return false;
}

bool replace_root_file(const char* path, const char* data) {
    if (ensure_parent_directories(&g_root, path) != fs::fat32::Status::Ok) {
        return false;
    }

    ::install::reliable_file::Paths paths{};
    if (!reliable_state_paths(path, &paths)) {
        terminal::write("[INSTALL][STATE] unsupported transactional path=");
        terminal::println(path == nullptr ? "(null)" : path);
        return false;
    }

    const size_t size = text_length(data);
    const ::install::reliable_file::Status status =
        ::install::fat32_reliable_file::replace(
            &g_root, paths, data, size);
    if (status != ::install::reliable_file::Status::Ok) {
        terminal::write("[INSTALL][STATE] path=");
        terminal::write(path);
        terminal::write(" status=");
        terminal::println(::install::reliable_file::status_message(status));
        return false;
    }
    return true;
}

bool write_profile(const InstallProfile& profile) {
    const char* locale = profile.language == Language::Polish
        ? "LANG=pl-PL\n" : "LANG=en-US\n";
    if (!replace_root_file("/etc/locale.cfg", locale)) return false;

    char user_config[256]{};
    append_text(user_config, sizeof(user_config), "USERNAME=");
    append_text(user_config, sizeof(user_config), profile.username);
    append_text(user_config, sizeof(user_config), "\nPASSWORD_REQUIRED=");
    append_text(user_config, sizeof(user_config),
                profile.password_required ? "1" : "0");
    append_text(user_config, sizeof(user_config), "\nPASSWORD_HASH=");
    char hash_text[17]{};
    u64_to_hex(profile.password_required
                   ? credential_hash(profile.username, profile.password)
                   : 0U,
               hash_text);
    append_text(user_config, sizeof(user_config), hash_text);
    append_text(user_config, sizeof(user_config),
                "\nHASH_SCHEME=FNV1A64-DEV\n");
    return replace_root_file("/etc/user.cfg", user_config);
}

void draw_progress(size_t stage, const char* label) {
    draw_brand_header("INSTALLING");
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t x = width / 2 - 280;
    graphics::fill_rect(x, 180, 560, 230, kPanel);
    graphics::draw_rect(x, 180, 560, 230, kBorder, 1U);
    graphics::draw_text(x + 30, 215, "INSTALLING KUROGANEOS",
                        kText, kPanel, 2U, true);
    graphics::draw_text(x + 30, 265, label, kMuted, kPanel, 1U, true);
    graphics::fill_rect(x + 30, 330, 500, 18, UINT32_C(0x252930));
    const int32_t progress = static_cast<int32_t>((stage * 500U) / 9U);
    graphics::fill_rect(x + 30, 330, progress, 18, kAccent);
    char step[32] = "STEP ";
    char number[8]{};
    u64_to_decimal(stage, number, sizeof(number));
    append_text(step, sizeof(step), number);
    append_text(step, sizeof(step), " / 9");
    graphics::draw_text(x + 30, 365, step, kAccentBright, kPanel, 1U, true);
}

size_t choose_disk() {
    const size_t count = storage::ahci::device_count();
    if (count == 0U) fail("NO WRITABLE SATA/AHCI DISK DETECTED");
    size_t selected = 0U;
    for (;;) {
        draw_setup_title("SELECT TARGET DISK",
                         "THE SELECTED DISK WILL BE ERASED");
        const int32_t width = static_cast<int32_t>(graphics::width());
        const int32_t x = width / 2 - 250;
        const storage::ahci::DeviceInfo* info =
            storage::ahci::device_info_at(selected);
        graphics::fill_rect(x, 220, 500, 120, kPanelRaised);
        graphics::draw_rect(x, 220, 500, 120, kAccent, 2U);
        graphics::draw_text(x + 24, 242,
                            info != nullptr ? info->model : "UNKNOWN SATA DISK",
                            kText, kPanelRaised, 1U, true);
        char size_line[64] = "SIZE: ";
        char size_text[24]{};
        const uint64_t mib = info != nullptr
            ? (info->sector_count * info->sector_size) / (1024U * 1024U)
            : 0U;
        u64_to_decimal(mib, size_text, sizeof(size_text));
        append_text(size_line, sizeof(size_line), size_text);
        append_text(size_line, sizeof(size_line), " MiB");
        graphics::draw_text(x + 24, 276, size_line,
                            kMuted, kPanelRaised, 1U, true);
        char index_line[64] = "DISK ";
        char current[16]{};
        char total[16]{};
        u64_to_decimal(selected + 1U, current, sizeof(current));
        u64_to_decimal(count, total, sizeof(total));
        append_text(index_line, sizeof(index_line), current);
        append_text(index_line, sizeof(index_line), " / ");
        append_text(index_line, sizeof(index_line), total);
        graphics::draw_text(x + 24, 306, index_line,
                            kAccentBright, kPanelRaised, 1U, true);
        draw_footer("UP/DOWN: CHANGE DISK   ENTER: SELECT");
        const auto event = wait_key();
        if (event.key == drivers::keyboard::KeyCode::ArrowUp) {
            selected = selected == 0U ? count - 1U : selected - 1U;
        } else if (event.key == drivers::keyboard::KeyCode::ArrowDown ||
                   event.key == drivers::keyboard::KeyCode::Tab) {
            selected = (selected + 1U) % count;
        } else if (event.key == drivers::keyboard::KeyCode::Enter ||
                   event.key == drivers::keyboard::KeyCode::KeypadEnter) {
            return selected;
        }
    }
}

bool confirm_erase() {
    char confirmation[16]{};
    size_t length = 0U;
    const char* error = nullptr;
    for (;;) {
        draw_input_page(
            "CONFIRM INSTALLATION",
            "TYPE INSTALL TO ERASE THE SELECTED DISK",
            confirmation, false, error);
        const auto event = wait_key();
        error = nullptr;
        if (event.key == drivers::keyboard::KeyCode::Escape) return false;
        if (event.key == drivers::keyboard::KeyCode::Backspace) {
            if (length != 0U) confirmation[--length] = '\0';
            continue;
        }
        if (event.key == drivers::keyboard::KeyCode::Enter ||
            event.key == drivers::keyboard::KeyCode::KeypadEnter) {
            if (strings_equal(confirmation, "INSTALL")) return true;
            error = "TYPE INSTALL EXACTLY OR ESC TO GO BACK";
            continue;
        }
        const char ch = ascii_upper(event.character);
        if (ch >= 0x20 && ch <= 0x7E && length + 1U < sizeof(confirmation)) {
            confirmation[length++] = ch;
            confirmation[length] = '\0';
        }
    }
}

void draw_complete(const InstallProfile& profile) {
    draw_brand_header("INSTALL COMPLETE");
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t x = width / 2 - 300;
    graphics::fill_rect(x, 160, 600, 260, kPanel);
    graphics::draw_rect(x, 160, 600, 260, kAccent, 2U);
    graphics::draw_text(x + 34, 205, "KUROGANEOS IS INSTALLED",
                        kText, kPanel, 2U, true);
    char user_line[96] = "ACCOUNT: ";
    append_text(user_line, sizeof(user_line), profile.username);
    graphics::draw_text(x + 34, 260, user_line,
                        kAccentBright, kPanel, 1U, true);
    graphics::draw_text(x + 34, 292,
                        profile.password_required
                            ? "LOGIN PASSWORD: ENABLED"
                            : "LOGIN PASSWORD: DISABLED",
                        kMuted, kPanel, 1U, true);
    graphics::draw_text(x + 34, 340,
                        "REMOVE INSTALL MEDIA AND REBOOT",
                        kText, kPanel, 1U, true);
}

} // namespace

void run_interactive(
    const void* package_bytes,
    size_t package_size) {
    package::View payload{};
    const package::Status package_status =
        package::parse(package_bytes, package_size, &payload);
    if (package_status != package::Status::Ok) {
        fail(package::status_message(package_status));
    }

    terminal::set_framebuffer_output(false);
    terminal::println("[SETUP] KuroganeOS 3.3 dev media detected");
    terminal::write("[SETUP] package files: ");
    terminal::write_u64(payload.file_count);
    terminal::println();
    terminal::println("[TEST] installer_package_preflight: PASS");

    const size_t mode = choose_two(
        "WELCOME TO KUROGANEOS",
        "DEVELOPMENT / BETA RELEASE MEDIA",
        "TRY KUROGANEOS",
        "RUN A READ-ONLY LIVE SESSION WITHOUT INSTALLING",
        "INSTALL KUROGANEOS",
        "CONFIGURE A USER AND DEPLOY TO A DISK");

    if (mode == 0U) {
        const fs::root_volume::Status live_status =
            fs::root_volume::initialize_live_package(package_bytes, package_size);
        if (live_status != fs::root_volume::Status::Ok) {
            fail(fs::root_volume::status_message(live_status));
        }
        terminal::println("[TEST] live_package_root: PASS");
        terminal::println("[TEST] setup_try_mode: PASS");
        return;
    }

    InstallProfile profile{};
    const size_t language = choose_two(
        "CHOOSE LANGUAGE / WYBIERZ JEZYK",
        "INSTALLER AND LOGIN PROFILE",
        "ENGLISH", "EN-US SYSTEM PROFILE",
        "POLSKI", "PL-PL PROFIL SYSTEMU");
    profile.language = language == 1U ? Language::Polish : Language::English;

    copy_text(profile.username, sizeof(profile.username), "user");
    if (!read_input(
            profile.language == Language::Polish
                ? "NAZWA UZYTKOWNIKA" : "USER NAME",
            profile.language == Language::Polish
                ? "UTWORZ LOKALNE KONTO" : "CREATE A LOCAL ACCOUNT",
            profile.username, sizeof(profile.username), false, true)) {
        fail("ACCOUNT SETUP CANCELLED");
    }

    const size_t password_mode = choose_two(
        profile.language == Language::Polish
            ? "ZABEZPIECZENIE KONTA" : "ACCOUNT SECURITY",
        profile.username,
        profile.language == Language::Polish ? "BEZ HASLA" : "NO PASSWORD",
        profile.language == Language::Polish
            ? "ENTER OD RAZU OTWIERA SESJE" : "ENTER OPENS THE SESSION DIRECTLY",
        profile.language == Language::Polish ? "USTAW HASLO" : "USE PASSWORD",
        profile.language == Language::Polish
            ? "LOGIN BEDZIE WYMAGAL HASLA" : "LOGIN WILL REQUIRE A PASSWORD");
    profile.password_required = password_mode == 1U;
    if (profile.password_required &&
        !read_input(
            profile.language == Language::Polish ? "HASLO" : "PASSWORD",
            profile.language == Language::Polish
                ? "WPISZ HASLO DO KONTA" : "ENTER THE ACCOUNT PASSWORD",
            profile.password, sizeof(profile.password), true, false)) {
        fail("PASSWORD SETUP CANCELLED");
    }

    const storage::block::Device* target = nullptr;
    for (;;) {
        const size_t target_index = choose_disk();
        target = storage::ahci::device_at(target_index);
        if (target == nullptr) fail("SELECTED DISK DISAPPEARED");
        if (confirm_erase()) {
            terminal::println("[TEST] installer_confirmation: PASS");
            break;
        }
        terminal::println("[TEST] installer_cancel_safe: PASS");
    }

    draw_progress(1U, "PARTITIONING TARGET DISK");
    disk_layout::Layout layout{};
    const disk_layout::Status layout_status =
        disk_layout::prepare_install_target(target, &layout);
    if (layout_status != disk_layout::Status::Ok) {
        fail(disk_layout::status_message(layout_status));
    }
    terminal::println("installer stage 1/9: target confirmed and GPT written");

    draw_progress(2U, "VALIDATING PARTITION TABLE");
    storage::gpt::Table table{};
    if (storage::gpt::parse_primary(target, &table).status !=
        storage::gpt::Status::Ok || table.partition_count != 2U) {
        fail("WRITTEN GPT DID NOT VALIDATE");
    }
    if (storage::partition::initialize(
            &g_esp_partition, target, layout.esp_first_lba,
            layout.esp_sector_count) != storage::block::Status::Ok ||
        storage::partition::initialize(
            &g_root_partition, target, layout.root_first_lba,
            layout.root_sector_count) != storage::block::Status::Ok) {
        fail("PARTITION VIEWS COULD NOT BE CREATED");
    }
    terminal::println("installer stage 2/9: GPT validated and partition views ready");

    draw_progress(3U, "FORMATTING ESP AND ROOT");
    const fs::fat32::Status esp_format = fs::fat32::format(
        storage::partition::as_block_device(&g_esp_partition),
        "KURO_ESP", 1U, static_cast<uint32_t>(layout.esp_first_lba));
    if (esp_format != fs::fat32::Status::Ok) {
        terminal::write("[INSTALL][FORMAT] ESP status=");
        terminal::println(fs::fat32::status_message(esp_format));
        fail("ESP FAT32 FORMATTING FAILED");
    }
    const fs::fat32::Status root_format = fs::fat32::format(
        storage::partition::as_block_device(&g_root_partition),
        "KURO_ROOT", 8U, static_cast<uint32_t>(layout.root_first_lba));
    if (root_format != fs::fat32::Status::Ok) {
        terminal::write("[INSTALL][FORMAT] ROOT status=");
        terminal::println(fs::fat32::status_message(root_format));
        fail("ROOT FAT32 FORMATTING FAILED");
    }
    terminal::println("installer stage 3/9: filesystems formatted");

    draw_progress(4U, "MOUNTING NEW FILESYSTEMS");
    const fs::fat32::Status esp_mount = fs::fat32::mount(
        &g_esp, storage::partition::as_block_device(&g_esp_partition));
    if (esp_mount != fs::fat32::Status::Ok) {
        terminal::write("[INSTALL][MOUNT] ESP status=");
        terminal::println(fs::fat32::status_message(esp_mount));
        fail("ESP MOUNT FAILED");
    }
    const fs::fat32::Status root_mount = fs::fat32::mount(
        &g_root, storage::partition::as_block_device(&g_root_partition));
    if (root_mount != fs::fat32::Status::Ok) {
        terminal::write("[INSTALL][MOUNT] ROOT status=");
        terminal::println(fs::fat32::status_message(root_mount));
        fail("ROOT MOUNT FAILED");
    }
    if (!ensure_root_layout()) {
        fail("ROOT DIRECTORY LAYOUT CREATION FAILED");
    }
    terminal::println("installer stage 4/9: fresh filesystems mounted and base layout created");

    // The base filesystem contract above owns intentionally empty directories.
    // Package-specific parents stay data-driven so nested payload paths such as
    // /etc/ssl/certs.pem do not require installer changes.
    draw_progress(5U, "COPYING ROOT SYSTEM PAYLOAD");
    if (!deploy_destination(payload, package::DESTINATION_ROOT)) {
        fail("ROOT PAYLOAD COPY FAILED - SEE SERIAL LOG");
    }
    if (fs::fat32::sync(&g_root) != fs::fat32::Status::Ok) {
        fail("ROOT PAYLOAD SYNC FAILED");
    }
    terminal::println("installer stage 5/9: root payload copied and synced");

    // Installer-generated state is committed only after the immutable root
    // payload is durable. The same VDI remains retryable after any later error.
    draw_progress(6U, "WRITING USER AND FIRST-BOOT STATE");
    if (!write_profile(profile) ||
        !replace_root_file("/etc/first.run", "pending\n") ||
        fs::fat32::sync(&g_root) != fs::fat32::Status::Ok) {
        fail("PROFILE OR FIRST-BOOT COMMIT FAILED");
    }
    terminal::println("installer stage 6/9: profile and first-boot state committed");

    // Activate UEFI boot only after the root volume is complete and durable.
    // This mirrors the transactional shape used by mature OS installers:
    // prepare root first, publish bootability last.
    draw_progress(7U, "ACTIVATING UEFI BOOT PAYLOAD");
    if (!deploy_destination(payload, package::DESTINATION_ESP)) {
        fail("UEFI PAYLOAD COPY FAILED - SEE SERIAL LOG");
    }
    if (fs::fat32::sync(&g_esp) != fs::fat32::Status::Ok) {
        fail("UEFI PAYLOAD SYNC FAILED");
    }
    terminal::println("installer stage 7/9: UEFI payload activated and synced");

    draw_progress(8U, "VERIFYING INSTALLED PAYLOAD");
    if (!verify_destination(payload, package::DESTINATION_ROOT) ||
        !verify_destination(payload, package::DESTINATION_ESP)) {
        fail("INSTALLED FILE VERIFICATION FAILED - SEE SERIAL LOG");
    }
    if (fs::fat32::sync(&g_root) != fs::fat32::Status::Ok ||
        fs::fat32::sync(&g_esp) != fs::fat32::Status::Ok) {
        fail("FINAL FILESYSTEM SYNC FAILED");
    }
    terminal::println("installer stage 8/9: installed payload verified");

    draw_progress(9U, "INSTALLATION COMPLETE");
    terminal::println("[TEST] installer_gpt: PASS");
    terminal::println("[TEST] installer_filesystems: PASS");
    terminal::println("[TEST] installer_root_payload: PASS");
    terminal::println("[TEST] installer_uefi_bootloader: PASS");
    terminal::println("[TEST] installer_profile: PASS");
    terminal::println("[TEST] installer_payload_verify: PASS");
    terminal::println("[TEST] installer_complete: PASS");
    terminal::println("installer stage 9/9: installation complete");
    draw_complete(profile);
    halt_forever();
}

} // namespace install::installer
