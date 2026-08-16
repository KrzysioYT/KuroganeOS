#include "shell.hpp"

#include "../apps/framework.hpp"
#include "../abi/service.hpp"
#include "../arch/x86_64/io.hpp"
#include "../core/string.hpp"
#include "../drivers/pci.hpp"
#include "../drivers/core/device_manager.hpp"
#include "../drivers/core/driver_manager.hpp"
#include "../drivers/rtc.hpp"
#include "../fs/ramfs.hpp"
#include "../fs/root_volume.hpp"
#include "../memory/allocator.hpp"
#include "../memory/physical_memory.hpp"
#include "../net/service.hpp"
#include "../storage/ahci.hpp"
#include "../task/scheduler.hpp"
#include "../terminal.hpp"
#include "../../common/version.h"

#include <stddef.h>
#include <stdint.h>

namespace shell {

namespace {
constexpr size_t kLineCapacity = 256;
constexpr size_t kMaximumArguments = 16;
constexpr size_t kHistoryCapacity = 16;

char g_line[kLineCapacity]{};
size_t g_line_length = 0;
bool g_initialized = false;
char g_current_directory[fs::RAMFS_MAX_PATH_LENGTH + 1] = "/";
char g_history[kHistoryCapacity][kLineCapacity]{};
size_t g_history_count = 0;
size_t g_history_next = 0;
bool g_experimental_gui_enabled = false;

void print_error(const char* subsystem, const char* message) {
    terminal::write("error");
    if (subsystem) {
        terminal::write(" [");
        terminal::write(subsystem);
        terminal::write("]");
    }
    terminal::write(": ");
    terminal::println(message);
}

bool component_equals(
    const char* component,
    size_t component_length,
    const char* literal
) {
    if (!component || !literal) {
        return false;
    }
    size_t literal_length = 0;
    while (literal[literal_length] != '\0') {
        ++literal_length;
    }
    if (component_length != literal_length) {
        return false;
    }
    for (size_t i = 0; i < component_length; ++i) {
        if (component[i] != literal[i]) {
            return false;
        }
    }
    return true;
}

fs::Status consume_path_components(
    const char* path,
    char* output,
    size_t& output_length,
    size_t* previous_lengths,
    size_t& depth
) {
    if (!path || !output || !previous_lengths) {
        return fs::Status::InvalidArgument;
    }

    size_t raw_length = 0;
    while (path[raw_length] != '\0') {
        if (raw_length == fs::RAMFS_MAX_PATH_LENGTH) {
            return fs::Status::PathTooLong;
        }
        ++raw_length;
    }

    size_t position = 0;
    while (position < raw_length) {
        while (position < raw_length && path[position] == '/') {
            ++position;
        }
        if (position == raw_length) {
            break;
        }

        const size_t start = position;
        while (position < raw_length && path[position] != '/') {
            ++position;
        }
        const size_t length = position - start;
        if (length > fs::RAMFS_MAX_NAME_LENGTH) {
            return fs::Status::NameTooLong;
        }
        if (component_equals(path + start, length, ".")) {
            continue;
        }
        if (component_equals(path + start, length, "..")) {
            if (depth > 0) {
                output_length = previous_lengths[--depth];
                output[output_length] = '\0';
            }
            continue;
        }
        if (depth >= fs::RAMFS_MAX_PATH_DEPTH) {
            return fs::Status::PathTooDeep;
        }

        const size_t separator = output_length == 1 ? 0 : 1;
        if (output_length > fs::RAMFS_MAX_PATH_LENGTH - separator ||
            length >
                fs::RAMFS_MAX_PATH_LENGTH - output_length - separator) {
            return fs::Status::PathTooLong;
        }
        previous_lengths[depth++] = output_length;
        if (separator != 0) {
            output[output_length++] = '/';
        }
        for (size_t i = 0; i < length; ++i) {
            output[output_length++] = path[start + i];
        }
        output[output_length] = '\0';
    }
    return fs::Status::Ok;
}

fs::Status resolve_user_path(const char* path, char* output) {
    if (!path || !output || path[0] == '\0') {
        return fs::Status::InvalidPath;
    }

    output[0] = '/';
    output[1] = '\0';
    size_t output_length = 1;
    size_t depth = 0;
    size_t previous_lengths[fs::RAMFS_MAX_PATH_DEPTH]{};

    if (path[0] != '/') {
        const fs::Status base_status = consume_path_components(
            g_current_directory,
            output,
            output_length,
            previous_lengths,
            depth);
        if (base_status != fs::Status::Ok) {
            return base_status;
        }
    }
    return consume_path_components(
        path, output, output_length, previous_lengths, depth);
}

bool resolve_or_report(const char* path, char* output) {
    const fs::Status status = resolve_user_path(path, output);
    if (status == fs::Status::Ok) {
        return true;
    }
    print_error("path", fs::status_message(status));
    return false;
}

void reset_history() {
    for (size_t i = 0; i < kHistoryCapacity; ++i) {
        g_history[i][0] = '\0';
    }
    g_history_count = 0;
    g_history_next = 0;
}

void remember_history(const char* line) {
    if (!line) {
        return;
    }
    bool has_command = false;
    for (size_t i = 0; line[i] != '\0'; ++i) {
        if (line[i] != ' ' && line[i] != '\t') {
            has_command = true;
            break;
        }
    }
    if (!has_command) {
        return;
    }

    kstd::copy(g_history[g_history_next], kLineCapacity, line);
    g_history_next = (g_history_next + 1) % kHistoryCapacity;
    if (g_history_count < kHistoryCapacity) {
        ++g_history_count;
    }
}

void command_history() {
    const size_t first =
        (g_history_next + kHistoryCapacity - g_history_count) %
        kHistoryCapacity;
    for (size_t i = 0; i < g_history_count; ++i) {
        terminal::write_u64(i + 1);
        terminal::write("  ");
        terminal::println(g_history[(first + i) % kHistoryCapacity]);
    }
}

void repair_current_directory() {
    fs::FileStat stat{};
    if (fs::stat_path(g_current_directory, &stat) != fs::Status::Ok ||
        stat.type != fs::EntryType::Directory) {
        kstd::copy(g_current_directory, sizeof(g_current_directory), "/");
    }
}

bool same_or_descendant_path(const char* path, const char* ancestor) {
    if (!path || !ancestor) {
        return false;
    }
    size_t index = 0;
    while (ancestor[index] != '\0') {
        if (path[index] != ancestor[index]) {
            return false;
        }
        ++index;
    }
    if (index == 1 && ancestor[0] == '/') {
        return true;
    }
    return path[index] == '\0' || path[index] == '/';
}

bool moved_current_directory(
    const char* source,
    const char* destination,
    char* updated
) {
    if (!same_or_descendant_path(g_current_directory, source)) {
        return false;
    }
    const size_t source_length = kstd::strlen(source);
    const size_t destination_length = kstd::strlen(destination);
    const size_t suffix_length =
        kstd::strlen(g_current_directory + source_length);
    if (destination_length > fs::RAMFS_MAX_PATH_LENGTH - suffix_length) {
        return false;
    }
    kstd::copy(updated, fs::RAMFS_MAX_PATH_LENGTH + 1, destination);
    kstd::append(
        updated,
        fs::RAMFS_MAX_PATH_LENGTH + 1,
        g_current_directory + source_length);
    return kstd::strlen(updated) == destination_length + suffix_length;
}

size_t split_arguments(char* line, char** arguments, size_t capacity) {
    size_t count = 0;
    char* read = line;
    char* write = line;

    while (*read) {
        while (*read == ' ' || *read == '\t') {
            ++read;
        }
        if (*read == '\0' || count >= capacity) {
            break;
        }

        arguments[count++] = write;
        char quote = 0;
        while (*read) {
            if (!quote && (*read == ' ' || *read == '\t')) {
                break;
            }
            if ((*read == '\'' || *read == '"') &&
                (quote == 0 || quote == *read)) {
                quote = quote == 0 ? *read : 0;
                ++read;
                continue;
            }
            if (*read == '\\' && read[1] != '\0') {
                ++read;
            }
            *write++ = *read++;
        }
        while (*read == ' ' || *read == '\t') {
            ++read;
        }
        *write++ = '\0';
    }
    return count;
}

bool parse_signed(const char* text, int64_t& output) {
    if (!text || !*text) {
        return false;
    }
    const bool negative = *text == '-';
    if (negative) {
        ++text;
    } else if (*text == '+') {
        ++text;
    }
    uint64_t magnitude = 0;
    if (!kstd::parse_u64(text, magnitude, 0)) {
        return false;
    }
    if ((!negative && magnitude > static_cast<uint64_t>(INT64_MAX)) ||
        (negative && magnitude > static_cast<uint64_t>(INT64_MAX) + 1)) {
        return false;
    }
    if (negative) {
        output = magnitude == static_cast<uint64_t>(INT64_MAX) + 1
                     ? INT64_MIN
                     : -static_cast<int64_t>(magnitude);
    } else {
        output = static_cast<int64_t>(magnitude);
    }
    return true;
}

bool list_file(const char* name, const fs::FileStat* stat, void*) {
    terminal::write(stat->type == fs::EntryType::Directory ? "d " : "- ");
    terminal::write_u64(stat->size);
    terminal::write("\t");
    terminal::println(name);
    return true;
}

bool list_task(const scheduler::TaskStat* task, void*) {
    terminal::write_hex(task->id);
    terminal::write("\t");
    terminal::write(task->name);
    terminal::write("\tstate=");
    terminal::write_u64(static_cast<uint8_t>(task->state));
    terminal::write("\truns=");
    terminal::write_u64(task->run_count);
    terminal::println();
    return true;
}

bool list_application(const applications::Definition& application, void*) {
    terminal::write("  ");
    terminal::write(application.name);
    terminal::write(" - ");
    terminal::println(application.description);
    return true;
}

void command_help() {
    terminal::println("KuroganeOS commands:");
    terminal::println("  help clear version uname abi echo date uptime");
    terminal::println("  mem free pci device [list|info <id>] driver [list|info <name>]");
    terminal::println("  diskinfo net [ping] ip ifconfig route arp ping [IPv4]");
    terminal::println("  nslookup <name> tasks");
    if (g_experimental_gui_enabled) {
        terminal::println(
            "  apps run <app> gui  (EXPERIMENTAL boot=desktop only)");
    }
    terminal::println("  pwd cd <path> ls [path] cat <path> stat <path>");
    terminal::println("  touch <path> mkdir <path> rmdir <path>");
    terminal::println("  write <path> <text> cp <src> <dst> mv <src> <dst>");
    terminal::println("  rm [-r] <path> history whoami");
    terminal::println("  calc <number> <+|-|*|/|%> <number>");
    terminal::println("  reboot poweroff shutdown");
}

void command_abi() {
    const ku_abi_descriptor& value = abi::descriptor();
    terminal::write("application ABI ");
    terminal::write_u64(value.abi_version >> 16);
    terminal::put('.');
    terminal::write_u64(value.abi_version & UINT16_MAX);
    terminal::write(" descriptor=");
    terminal::write_u64(value.structure_size);
    terminal::write(" page=");
    terminal::write_u64(value.page_size);
    terminal::write(" features=");
    terminal::write_hex(value.available_features);
    terminal::println();
    terminal::println(
        abi::application_transport_available()
            ? "transport: available"
            : "transport: unavailable (runtime initialization failed)");
}

void command_mem() {
    terminal::write("heap total=");
    terminal::write_u64(memory::total_bytes());
    terminal::write(" used=");
    terminal::write_u64(memory::used_bytes());
    terminal::write(" free=");
    terminal::write_u64(memory::free_bytes());
    terminal::write(" allocations=");
    terminal::write_u64(memory::allocation_count());
    terminal::println();

    if (!memory::physical_memory_initialized()) {
        terminal::println("physical allocator: unavailable (legacy boot ABI)");
        return;
    }
    terminal::write("frames total=");
    terminal::write_u64(memory::total_frames());
    terminal::write(" used=");
    terminal::write_u64(memory::used_frames());
    terminal::write(" free=");
    terminal::write_u64(memory::free_frames());
    terminal::write(" reserved=");
    terminal::write_u64(memory::reserved_frames());
    terminal::println();
}

void command_pci() {
    terminal::write("PCI devices: ");
    terminal::write_u64(pci::device_count());
    terminal::println();
    for (size_t i = 0; i < pci::device_count(); ++i) {
        const pci::Device* device = pci::device_at(i);
        if (!device) {
            continue;
        }
        terminal::write_u64(device->address.bus);
        terminal::put(':');
        terminal::write_u64(device->address.slot);
        terminal::put('.');
        terminal::write_u64(device->address.function);
        terminal::write(" vendor=");
        terminal::write_hex(device->vendor_id);
        terminal::write(" device=");
        terminal::write_hex(device->device_id);
        terminal::write(" class=");
        terminal::write_hex(device->class_code);
        terminal::put(':');
        terminal::write_hex(device->subclass);
        terminal::println();
    }
}

void print_device(const drivers::device::Device& device) {
    terminal::write_u64(device.id);
    terminal::write("  ");
    terminal::write(drivers::device::type_name(device.type));
    terminal::write("  ");
    terminal::write(
        device.driver != drivers::device::INVALID_DRIVER_ID
            ? device.driver_name
            : "-");
    terminal::write("  ");
    terminal::println(drivers::device::status_name(device.status));
}

void command_device(size_t count, char** arguments) {
    if (count >= 2 && kstd::streq(arguments[1], "info")) {
        uint64_t requested = 0;
        if (count < 3 || !kstd::parse_u64(arguments[2], requested) ||
            requested > UINT32_MAX) {
            print_error("device", "usage: device info <id>");
            return;
        }
        const drivers::device::Device* device = drivers::device::get(
            static_cast<drivers::device::DeviceId>(requested));
        if (device == nullptr) {
            print_error("device", "device not found");
            return;
        }
        terminal::write("Device ");
        terminal::write_u64(device->id);
        terminal::println();
        terminal::write("Name: ");
        terminal::println(device->name);
        terminal::write("Bus: ");
        terminal::println(drivers::device::bus_name(device->bus));
        terminal::write("Class: ");
        terminal::println(drivers::device::type_name(device->type));
        terminal::write("Vendor: ");
        terminal::write_hex(device->vendor_id);
        terminal::write(" Device: ");
        terminal::write_hex(device->device_id);
        terminal::println();
        terminal::write("Driver: ");
        terminal::println(
            device->driver != drivers::device::INVALID_DRIVER_ID
                ? device->driver_name
                : "none");
        terminal::write("Status: ");
        terminal::println(drivers::device::status_name(device->status));
        terminal::write("Children: ");
        terminal::write_u64(device->child_count);
        terminal::println();
        return;
    }

    terminal::write("Device Manager: ");
    terminal::write_u64(drivers::device::count());
    terminal::println(" devices");
    terminal::println("ID  CLASS  DRIVER  STATUS");
    for (drivers::device::DeviceId id = 0;
         id < drivers::device::count();
         ++id) {
        const drivers::device::Device* device = drivers::device::get(id);
        if (device != nullptr) {
            print_device(*device);
        }
    }
}

void print_driver(const drivers::driver::Driver& driver) {
    terminal::write(driver.name);
    terminal::write("  ");
    terminal::write(drivers::driver::status_name(driver.status));
    terminal::write("  attached=");
    terminal::write_u64(driver.attached_count);
    terminal::println();
}

void command_driver(size_t count, char** arguments) {
    if (count >= 2 && kstd::streq(arguments[1], "info")) {
        if (count < 3) {
            print_error("driver", "usage: driver info <name>");
            return;
        }
        const drivers::driver::Driver* driver =
            drivers::driver::find(arguments[2]);
        if (driver == nullptr) {
            print_error("driver", "driver not found");
            return;
        }
        terminal::write("Driver: ");
        terminal::println(driver->name);
        terminal::write("Status: ");
        terminal::println(drivers::driver::status_name(driver->status));
        terminal::write("Probes: ");
        terminal::write_u64(driver->probe_count);
        terminal::write(" Attached: ");
        terminal::write_u64(driver->attached_count);
        terminal::write(" Failures: ");
        terminal::write_u64(driver->failure_count);
        terminal::println();
        return;
    }
    terminal::write("Driver Manager: ");
    terminal::write_u64(drivers::driver::count());
    terminal::println(" drivers");
    drivers::driver::visit(
        [](const drivers::driver::Driver& driver, void*) {
            print_driver(driver);
            return true;
        },
        nullptr);
}

void command_diskinfo() {
    terminal::write("Disks: ");
    terminal::write_u64(storage::ahci::device_count());
    terminal::println();
    for (size_t index = 0; index < storage::ahci::device_count(); ++index) {
        const storage::ahci::DeviceInfo* info =
            storage::ahci::device_info_at(index);
        if (info == nullptr) {
            continue;
        }
        terminal::write("Disk ");
        terminal::write_u64(index);
        terminal::write(" driver=AHCI model=");
        terminal::write(info->model);
        terminal::write(" blocks=");
        terminal::write_u64(info->sector_count);
        terminal::write(" block_size=");
        terminal::write_u64(info->sector_size);
        terminal::println(" status=READY");
    }
    if (fs::root_volume::mounted()) {
        terminal::write("RootFS: FAT32 label=");
        terminal::write(fs::root_volume::volume_label());
        terminal::write(" first_lba=");
        terminal::write_u64(fs::root_volume::first_lba());
        terminal::write(" blocks=");
        terminal::write_u64(fs::root_volume::sector_count());
        terminal::println(" mode=read-write vfs=READY");
    } else {
        terminal::println("RootFS: unavailable (kernel shell uses RAMFS)");
    }
}

void command_date() {
    rtc::DateTime value{};
    if (!rtc::read(value)) {
        print_error("rtc", "clock read failed");
        return;
    }
    terminal::write_u64(value.year);
    terminal::put('-');
    if (value.month < 10) terminal::put('0');
    terminal::write_u64(value.month);
    terminal::put('-');
    if (value.day < 10) terminal::put('0');
    terminal::write_u64(value.day);
    terminal::put(' ');
    if (value.hour < 10) terminal::put('0');
    terminal::write_u64(value.hour);
    terminal::put(':');
    if (value.minute < 10) terminal::put('0');
    terminal::write_u64(value.minute);
    terminal::put(':');
    if (value.second < 10) terminal::put('0');
    terminal::write_u64(value.second);
    terminal::println(" UTC");
}

void write_ipv4(const net::IPv4Address& address) {
    for (size_t i = 0; i < net::IPV4_ADDRESS_LENGTH; ++i) {
        if (i != 0) {
            terminal::put('.');
        }
        terminal::write_u64(address.bytes[i]);
    }
}

void write_mac(const net::MacAddress& address) {
    constexpr char hex[] = "0123456789abcdef";
    for (size_t index = 0U; index < net::MAC_ADDRESS_LENGTH; ++index) {
        if (index != 0U) terminal::put(':');
        terminal::put(hex[address.bytes[index] >> 4U]);
        terminal::put(hex[address.bytes[index] & UINT8_C(0x0f)]);
    }
}

bool parse_ipv4_text(const char* text, net::IPv4Address* output) {
    if (text == nullptr || output == nullptr) return false;
    net::IPv4Address result{};
    size_t cursor = 0U;
    for (size_t component = 0U; component < 4U; ++component) {
        if (text[cursor] < '0' || text[cursor] > '9') return false;
        uint16_t value = 0U;
        size_t digits = 0U;
        while (text[cursor] >= '0' && text[cursor] <= '9') {
            value = static_cast<uint16_t>(
                value * 10U + static_cast<uint16_t>(text[cursor] - '0'));
            if (++digits > 3U || value > 255U) return false;
            ++cursor;
        }
        result.bytes[component] = static_cast<uint8_t>(value);
        if (component != 3U) {
            if (text[cursor] != '.') return false;
            ++cursor;
        }
    }
    if (text[cursor] != '\0') return false;
    *output = result;
    return true;
}

void command_network(bool ping) {
    if (!net::service::ready()) {
        print_error("net", "network stack is not initialized");
        return;
    }
    if (ping) {
        static uint16_t sequence = 1;
        net::PingReply reply{};
        const auto status =
            net::service::ping_gateway(sequence++, &reply);
        if (status != net::Status::Ok) {
            print_error("net", net::status_message(status));
            return;
        }
        terminal::write("reply from ");
        write_ipv4(reply.source);
        terminal::write(": sequence=");
        terminal::write_u64(reply.sequence);
        terminal::write(" bytes=");
        terminal::write_u64(reply.payload_length);
        terminal::println();
        return;
    }

    const net::IPv4Config* config = net::service::configuration();
    net::NetworkStats stats{};
    const auto status = net::service::stats(&stats);
    if (!config || status != net::Status::Ok) {
        print_error("net", net::status_message(status));
        return;
    }
    terminal::write("interface ");
    terminal::write(net::service::interface_name());
    terminal::write(" address=");
    write_ipv4(config->address);
    terminal::write(" mask=");
    write_ipv4(config->netmask);
    terminal::write(" gateway=");
    write_ipv4(config->gateway);
    terminal::println();
    terminal::write("tx=");
    terminal::write_u64(stats.frames_transmitted);
    terminal::write(" rx=");
    terminal::write_u64(stats.frames_received);
    terminal::write(" drop=");
    terminal::write_u64(stats.dropped_frames);
    terminal::write(" icmp replies=");
    terminal::write_u64(stats.echo_replies_received);
    terminal::println();
}

void command_ping(const char* target) {
    if (!net::service::ready()) {
        print_error("ping", "network stack is not initialized");
        return;
    }
    static uint16_t sequence = 100U;
    net::PingReply reply{};
    net::Status status = net::Status::InvalidArgument;
    if (target == nullptr) {
        status = net::service::ping_gateway(sequence++, &reply);
    } else {
        net::IPv4Address address{};
        if (!parse_ipv4_text(target, &address)) {
            print_error("ping", "expected a dotted-decimal IPv4 address");
            return;
        }
        status = net::service::ping_address(address, sequence++, &reply);
    }
    if (status != net::Status::Ok) {
        print_error("ping", net::status_message(status));
        return;
    }
    terminal::write("reply from ");
    write_ipv4(reply.source);
    terminal::write(": sequence=");
    terminal::write_u64(reply.sequence);
    terminal::write(" bytes=");
    terminal::write_u64(reply.payload_length);
    terminal::println();
}

void command_route() {
    const net::IPv4Config* config = net::service::configuration();
    if (config == nullptr) {
        print_error("route", "network stack is not initialized");
        return;
    }
    terminal::write("connected ");
    write_ipv4(config->address);
    terminal::write(" mask ");
    write_ipv4(config->netmask);
    terminal::write(" dev ");
    terminal::println(net::service::interface_name());
    terminal::write("default via ");
    write_ipv4(config->gateway);
    terminal::write(" dev ");
    terminal::println(net::service::interface_name());
}

bool write_neighbor(const net::NeighborEntry* entry, void*) {
    write_ipv4(entry->ip);
    terminal::write(" at ");
    write_mac(entry->mac);
    terminal::println(" REACHABLE");
    return true;
}

void command_arp() {
    const net::Status status = net::service::list_neighbors(write_neighbor, nullptr);
    if (status != net::Status::Ok) {
        print_error("arp", net::status_message(status));
    }
}

void command_nslookup(const char* name) {
    if (name == nullptr) {
        print_error("nslookup", "usage: nslookup <name>");
        return;
    }
    net::IPv4Address address{};
    const net::Status status = net::service::resolve_a(name, &address);
    if (status != net::Status::Ok) {
        print_error("nslookup", net::status_message(status));
        return;
    }
    const net::IPv4Address* server = net::service::dns_server();
    terminal::write("server ");
    if (server != nullptr) write_ipv4(*server);
    terminal::write("\nname ");
    terminal::write(name);
    terminal::write(" address ");
    write_ipv4(address);
    terminal::println();
}

void command_cat(const char* path) {
    fs::FileStat stat{};
    auto status = fs::stat_path(path, &stat);
    if (status != fs::Status::Ok) {
        print_error("fs", fs::status_message(status));
        return;
    }
    if (stat.type != fs::EntryType::File) {
        print_error("fs", "path is a directory");
        return;
    }
    auto* buffer = static_cast<char*>(memory::kmalloc(stat.size + 1, 1));
    if (!buffer) {
        print_error("heap", "out of memory");
        return;
    }
    size_t bytes_read = 0;
    status = fs::read_file_data(path, buffer, stat.size, &bytes_read);
    if (status == fs::Status::Ok || stat.size == 0) {
        buffer[bytes_read] = '\0';
        terminal::write(buffer);
        if (bytes_read == 0 || buffer[bytes_read - 1] != '\n') {
            terminal::println();
        }
    } else {
        print_error("fs", fs::status_message(status));
    }
    memory::kfree(buffer);
}

void command_change_directory(const char* path) {
    char resolved[fs::RAMFS_MAX_PATH_LENGTH + 1];
    if (!resolve_or_report(path, resolved)) {
        return;
    }

    fs::FileStat stat{};
    const fs::Status status = fs::stat_path(resolved, &stat);
    if (status != fs::Status::Ok) {
        print_error("fs", fs::status_message(status));
        return;
    }
    if (stat.type != fs::EntryType::Directory) {
        print_error("fs", "not a directory");
        return;
    }
    kstd::copy(
        g_current_directory, sizeof(g_current_directory), resolved);
}

void command_stat(const char* path) {
    char resolved[fs::RAMFS_MAX_PATH_LENGTH + 1];
    if (!resolve_or_report(path, resolved)) {
        return;
    }

    fs::FileStat stat{};
    const fs::Status status = fs::stat_path(resolved, &stat);
    if (status != fs::Status::Ok) {
        print_error("fs", fs::status_message(status));
        return;
    }
    terminal::write(resolved);
    terminal::write(" type=");
    terminal::write(
        stat.type == fs::EntryType::Directory ? "directory" : "file");
    terminal::write(" size=");
    terminal::write_u64(stat.size);
    if (stat.type == fs::EntryType::Directory) {
        terminal::write(" children=");
        terminal::write_u64(stat.child_count);
    }
    terminal::println();
}

void command_rmdir(const char* path) {
    char resolved[fs::RAMFS_MAX_PATH_LENGTH + 1];
    if (!resolve_or_report(path, resolved)) {
        return;
    }

    fs::FileStat stat{};
    fs::Status status = fs::stat_path(resolved, &stat);
    if (status == fs::Status::Ok && stat.type != fs::EntryType::Directory) {
        status = fs::Status::NotDirectory;
    }
    if (status == fs::Status::Ok) {
        status = fs::remove_path(resolved, false);
    }
    if (status != fs::Status::Ok) {
        print_error("fs", fs::status_message(status));
        return;
    }
    repair_current_directory();
}

void command_copy(const char* source_path, const char* destination_path) {
    char source[fs::RAMFS_MAX_PATH_LENGTH + 1];
    char destination[fs::RAMFS_MAX_PATH_LENGTH + 1];
    if (!resolve_or_report(source_path, source) ||
        !resolve_or_report(destination_path, destination)) {
        return;
    }
    const fs::Status status = fs::copy_file(source, destination);
    if (status != fs::Status::Ok) {
        print_error("fs", fs::status_message(status));
    }
}

void command_move(const char* source_path, const char* destination_path) {
    char source[fs::RAMFS_MAX_PATH_LENGTH + 1];
    char destination[fs::RAMFS_MAX_PATH_LENGTH + 1];
    if (!resolve_or_report(source_path, source) ||
        !resolve_or_report(destination_path, destination)) {
        return;
    }

    const bool moves_cwd =
        same_or_descendant_path(g_current_directory, source);
    char updated_cwd[fs::RAMFS_MAX_PATH_LENGTH + 1]{};
    if (moves_cwd &&
        !moved_current_directory(source, destination, updated_cwd)) {
        print_error("path", "moved working directory path is too long");
        return;
    }

    const fs::Status status = fs::move_path(source, destination);
    if (status != fs::Status::Ok) {
        print_error("fs", fs::status_message(status));
        return;
    }
    if (moves_cwd) {
        kstd::copy(
            g_current_directory,
            sizeof(g_current_directory),
            updated_cwd);
    }
    repair_current_directory();
}

void command_write(size_t count, char** arguments, const char* path) {
    char content[kLineCapacity];
    content[0] = '\0';
    for (size_t i = 2; i < count; ++i) {
        if (i != 2) {
            kstd::append(content, sizeof(content), " ");
        }
        kstd::append(content, sizeof(content), arguments[i]);
    }
    const auto status = fs::write_file_data(
        path, content, kstd::strlen(content), true);
    if (status != fs::Status::Ok) {
        print_error("fs", fs::status_message(status));
    }
}

void command_calc(const char* left_text, const char* operation,
                  const char* right_text) {
    int64_t left;
    int64_t right;
    if (!parse_signed(left_text, left) || !parse_signed(right_text, right) ||
        !operation || operation[1] != '\0') {
        print_error("calc", "usage: calc <integer> <+|-|*|/|%> <integer>");
        return;
    }
    int64_t result = 0;
    switch (operation[0]) {
    case '+':
        if (__builtin_add_overflow(left, right, &result)) {
            print_error("calc", "integer overflow");
            return;
        }
        break;
    case '-':
        if (__builtin_sub_overflow(left, right, &result)) {
            print_error("calc", "integer overflow");
            return;
        }
        break;
    case '*':
        if (__builtin_mul_overflow(left, right, &result)) {
            print_error("calc", "integer overflow");
            return;
        }
        break;
    case '/':
        if (right == 0) {
            print_error("calc", "division by zero");
            return;
        }
        if (left == INT64_MIN && right == -1) {
            print_error("calc", "integer overflow");
            return;
        }
        result = left / right;
        break;
    case '%':
        if (right == 0) {
            print_error("calc", "division by zero");
            return;
        }
        if (left == INT64_MIN && right == -1) {
            result = 0;
            break;
        }
        result = left % right;
        break;
    default:
        print_error("calc", "unknown operator");
        return;
    }
    char buffer[32];
    kstd::format_i64(buffer, sizeof(buffer), result);
    terminal::println(buffer);
}

void reboot_delay() {
    for (size_t spin = 0; spin < 100000; ++spin) {
        arch::pause();
    }
}

void command_reboot() {
    terminal::println("rebooting...");

    size_t spin = 0;
    while ((arch::in8(0x64) & 0x02u) != 0 && spin < 100000) {
        arch::pause();
        ++spin;
    }
    if (spin < 100000) {
        arch::out8(0x64, 0xFE);
        reboot_delay();
    }

    // PCI reset control is the first fallback on modern chipsets.
    arch::out8(0xCF9, 0x02);
    arch::io_wait();
    arch::out8(0xCF9, 0x06);
    reboot_delay();

    // System Control Port A provides a final, widely supported reset path.
    const uint8_t control = arch::in8(0x92);
    arch::out8(0x92, static_cast<uint8_t>(control & 0xFEu));
    arch::io_wait();
    arch::out8(0x92, static_cast<uint8_t>((control & 0xFEu) | 0x01u));
    reboot_delay();

    print_error("reboot", "all hardware reset methods returned");
}

void command_poweroff() {
    terminal::println("powering off...");
    arch::out16(0x604, 0x2000);
    arch::out16(0xB004, 0x2000);
    print_error("poweroff", "emulator power-off ports returned");
}

void dispatch(size_t count, char** arguments) {
    if (count == 0) {
        return;
    }
    const char* command = arguments[0];
    if (kstd::streq(command, "help")) {
        command_help();
    } else if (kstd::streq(command, "clear")) {
        terminal::clear();
    } else if (kstd::streq(command, "version") ||
               kstd::streq(command, "uname")) {
        terminal::println(
            KUROGANE_PRODUCT_STRING " x86_64 UEFI");
    } else if (kstd::streq(command, "abi")) {
        command_abi();
    } else if (kstd::streq(command, "echo")) {
        for (size_t i = 1; i < count; ++i) {
            if (i != 1) terminal::put(' ');
            terminal::write(arguments[i]);
        }
        terminal::println();
    } else if (kstd::streq(command, "mem") ||
               kstd::streq(command, "free")) {
        command_mem();
    } else if (kstd::streq(command, "pci")) {
        command_pci();
    } else if (kstd::streq(command, "device")) {
        command_device(count, arguments);
    } else if (kstd::streq(command, "driver")) {
        command_driver(count, arguments);
    } else if (kstd::streq(command, "diskinfo")) {
        command_diskinfo();
    } else if (kstd::streq(command, "date")) {
        command_date();
    } else if (kstd::streq(command, "net")) {
        command_network(count >= 2 && kstd::streq(arguments[1], "ping"));
    } else if (kstd::streq(command, "ip") ||
               kstd::streq(command, "ifconfig")) {
        command_network(false);
    } else if (kstd::streq(command, "route")) {
        command_route();
    } else if (kstd::streq(command, "arp")) {
        command_arp();
    } else if (kstd::streq(command, "ping")) {
        command_ping(count >= 2 ? arguments[1] : nullptr);
    } else if (kstd::streq(command, "nslookup")) {
        command_nslookup(count >= 2 ? arguments[1] : nullptr);
    } else if (kstd::streq(command, "uptime")) {
        terminal::write_u64(scheduler::now());
        terminal::println(" ticks");
    } else if (kstd::streq(command, "tasks")) {
        scheduler::list(list_task, nullptr);
    } else if (g_experimental_gui_enabled &&
               kstd::streq(command, "apps")) {
        applications::list(list_application, nullptr);
    } else if (g_experimental_gui_enabled &&
               kstd::streq(command, "run") && count >= 2) {
        const auto status = applications::launch(
            arguments[1], count >= 3 ? arguments[2] : "");
        if (status != applications::Status::Ok) {
            print_error("apps", applications::status_message(status));
        }
    } else if (g_experimental_gui_enabled &&
               kstd::streq(command, "gui")) {
        const auto status = applications::launch("desktop");
        if (status != applications::Status::Ok) {
            print_error("apps", applications::status_message(status));
        }
    } else if (kstd::streq(command, "pwd")) {
        terminal::println(g_current_directory);
    } else if (kstd::streq(command, "cd") && count == 2) {
        command_change_directory(arguments[1]);
    } else if (kstd::streq(command, "ls")) {
        char resolved[fs::RAMFS_MAX_PATH_LENGTH + 1];
        const char* requested = count >= 2
            ? arguments[1]
            : g_current_directory;
        if (!resolve_or_report(requested, resolved)) {
            return;
        }
        const auto status = fs::list_directory(
            resolved, list_file, nullptr);
        if (status != fs::Status::Ok &&
            status != fs::Status::IterationStopped) {
            print_error("fs", fs::status_message(status));
        }
    } else if (kstd::streq(command, "cat") && count == 2) {
        char resolved[fs::RAMFS_MAX_PATH_LENGTH + 1];
        if (resolve_or_report(arguments[1], resolved)) {
            command_cat(resolved);
        }
    } else if (kstd::streq(command, "stat") && count == 2) {
        command_stat(arguments[1]);
    } else if (kstd::streq(command, "touch") && count == 2) {
        char resolved[fs::RAMFS_MAX_PATH_LENGTH + 1];
        if (!resolve_or_report(arguments[1], resolved)) {
            return;
        }
        const auto status = fs::create_file_at(resolved);
        if (status != fs::Status::Ok && status != fs::Status::AlreadyExists) {
            print_error("fs", fs::status_message(status));
        }
    } else if (kstd::streq(command, "mkdir") && count == 2) {
        char resolved[fs::RAMFS_MAX_PATH_LENGTH + 1];
        if (!resolve_or_report(arguments[1], resolved)) {
            return;
        }
        const auto status = fs::create_directory_at(resolved);
        if (status != fs::Status::Ok) {
            print_error("fs", fs::status_message(status));
        }
    } else if (kstd::streq(command, "rmdir") && count == 2) {
        command_rmdir(arguments[1]);
    } else if (kstd::streq(command, "write") && count >= 3) {
        char resolved[fs::RAMFS_MAX_PATH_LENGTH + 1];
        if (resolve_or_report(arguments[1], resolved)) {
            command_write(count, arguments, resolved);
        }
    } else if (kstd::streq(command, "cp") && count == 3) {
        command_copy(arguments[1], arguments[2]);
    } else if (kstd::streq(command, "mv") && count == 3) {
        command_move(arguments[1], arguments[2]);
    } else if (kstd::streq(command, "rm") &&
               (count == 2 || (count == 3 &&
                                kstd::streq(arguments[1], "-r")))) {
        const bool recursive = count == 3;
        char resolved[fs::RAMFS_MAX_PATH_LENGTH + 1];
        if (!resolve_or_report(
                recursive ? arguments[2] : arguments[1], resolved)) {
            return;
        }
        const auto status = fs::remove_path(resolved, recursive);
        if (status != fs::Status::Ok) {
            print_error("fs", fs::status_message(status));
        } else {
            repair_current_directory();
        }
    } else if (kstd::streq(command, "history")) {
        command_history();
    } else if (kstd::streq(command, "whoami")) {
        terminal::println("kernel");
    } else if (kstd::streq(command, "calc") && count == 4) {
        command_calc(arguments[1], arguments[2], arguments[3]);
    } else if (kstd::streq(command, "reboot")) {
        command_reboot();
    } else if (kstd::streq(command, "poweroff") ||
               kstd::streq(command, "shutdown")) {
        command_poweroff();
    } else {
        print_error("shell", "unknown command or invalid arguments");
    }
}
} // namespace

void initialize(bool experimental_gui_enabled) {
    g_line_length = 0;
    g_line[0] = '\0';
    kstd::copy(g_current_directory, sizeof(g_current_directory), "/");
    reset_history();
    g_experimental_gui_enabled = experimental_gui_enabled;
    g_initialized = true;
    show_prompt();
}

void feed(char character) {
    if (!g_initialized) {
        initialize(false);
    }
    if (character == '\r' || character == '\n') {
        terminal::println();
        g_line[g_line_length] = '\0';
        execute(g_line);
        g_line_length = 0;
        g_line[0] = '\0';
        if (!applications::running()) {
            show_prompt();
        }
        return;
    }
    if (character == '\b' || character == 0x7F) {
        if (g_line_length > 0) {
            --g_line_length;
            g_line[g_line_length] = '\0';
            terminal::backspace();
        }
        return;
    }
    if (character == 0x15) {
        while (g_line_length > 0) {
            --g_line_length;
            terminal::backspace();
        }
        g_line[0] = '\0';
        return;
    }
    if (character < 0x20 || character > 0x7E ||
        g_line_length + 1 >= kLineCapacity) {
        return;
    }
    g_line[g_line_length++] = character;
    g_line[g_line_length] = '\0';
    terminal::put(character);
}

void execute(const char* command_line) {
    if (!command_line) {
        return;
    }
    remember_history(command_line);
    char buffer[kLineCapacity];
    kstd::copy(buffer, sizeof(buffer), command_line);
    char* arguments[kMaximumArguments]{};
    const size_t count =
        split_arguments(buffer, arguments, kMaximumArguments);
    dispatch(count, arguments);
}

void show_prompt() {
    terminal::write("kurogane:");
    terminal::write(g_current_directory);
    terminal::write(" $ ");
}

bool initialized() {
    return g_initialized;
}

const char* current_directory() {
    return g_current_directory;
}

} // namespace shell
