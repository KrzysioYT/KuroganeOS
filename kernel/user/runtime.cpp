#include "runtime.hpp"

#include <kurogane/audio.h>
#include <kurogane/desktop.h>
#include <kurogane/device.h>
#include <kurogane/event.h>
#include <kurogane/filesystem.h>
#include <kurogane/ipc.h>
#include <kurogane/network.h>
#include <kurogane/shared_memory.h>
#include <kurogane/service.h>
#include <kurogane/status.h>
#include <kurogane/syscall.h>
#include <kurogane/system.h>
#include <kurogane/ui.h>

#include "elf_loader.hpp"
#include "console.hpp"
#include "../arch/x86_64/gdt.hpp"
#include "../core/log.hpp"
#include "../core/system_metrics.hpp"
#include "../drivers/audio/ac97.hpp"
#include "../drivers/core/device_manager.hpp"
#include "../drivers/pit.hpp"
#include "../fs/root_volume.hpp"
#include "../ipc/channel.hpp"
#include "../ipc/event.hpp"
#include "../ipc/shared_memory.hpp"
#include "../memory/allocator.hpp"
#include "../memory/kernel_virtual_memory.hpp"
#include "../memory/physical_memory.hpp"
#include "../net/service.hpp"
#include "../net/socket.hpp"
#include "../task/process.hpp"
#include "../task/thread.hpp"
#include "../terminal.hpp"
#include "../drivers/framebuffer.hpp"
#include "../ui/ui.hpp"
#include "../ui/window_manager.hpp"

#define initialize legacy_initialize
#include "runtime_base.inc"
#undef initialize

namespace user::runtime {
namespace {

constexpr uint64_t kSharedRegionBase =
    elf::USER_REGION_BASE + UINT64_C(0x03800000);
constexpr size_t kMaximumSharedMappingsPerContext = 8U;
constexpr size_t kMaximumRuntimeSharedMappings =
    kMaximumContexts * kMaximumSharedMappingsPerContext;
constexpr uint64_t kSharedMappingStride =
    static_cast<uint64_t>(ipc::shared_memory::MAX_PAGES_PER_OBJECT) *
    memory::virtual_memory::PAGE_SIZE;

struct RuntimeSharedMapping {
    Context* context;
    ipc::shared_memory::Handle handle;
    uint64_t base;
    size_t page_count;
    bool active;
};

RuntimeSharedMapping g_shared_mappings[kMaximumRuntimeSharedMappings]{};

ku_status_t ipc_status(ipc::Status status) {
    switch (status) {
        case ipc::Status::Ok: return KU_STATUS_OK;
        case ipc::Status::AlreadyInitialized: return KU_STATUS_OK;
        case ipc::Status::InvalidArgument:
        case ipc::Status::InvalidName:
        case ipc::Status::NameTooLong:
        case ipc::Status::StaleHandle: return KU_STATUS_INVALID_ARGUMENT;
        case ipc::Status::AlreadyExists: return KU_STATUS_ALREADY_EXISTS;
        case ipc::Status::NotFound: return KU_STATUS_NOT_FOUND;
        case ipc::Status::AccessDenied: return KU_STATUS_ACCESS_DENIED;
        case ipc::Status::CapacityReached: return KU_STATUS_OUT_OF_MEMORY;
        case ipc::Status::WouldBlock: return KU_STATUS_WOULD_BLOCK;
        case ipc::Status::VersionMismatch: return KU_STATUS_VERSION_MISMATCH;
        case ipc::Status::NotInitialized:
        case ipc::Status::PeerClosed: return KU_STATUS_BAD_STATE;
    }
    return KU_STATUS_IO_ERROR;
}

ku_status_t shared_memory_status(ipc::shared_memory::Status status) {
    using SharedStatus = ipc::shared_memory::Status;
    switch (status) {
        case SharedStatus::Ok:
        case SharedStatus::AlreadyInitialized: return KU_STATUS_OK;
        case SharedStatus::InvalidArgument:
        case SharedStatus::StaleHandle: return KU_STATUS_INVALID_ARGUMENT;
        case SharedStatus::OutOfRange: return KU_STATUS_OUT_OF_RANGE;
        case SharedStatus::OutOfMemory:
        case SharedStatus::CapacityReached: return KU_STATUS_OUT_OF_MEMORY;
        case SharedStatus::AccessDenied: return KU_STATUS_ACCESS_DENIED;
        case SharedStatus::AlreadyGranted: return KU_STATUS_ALREADY_EXISTS;
        case SharedStatus::NotFound: return KU_STATUS_NOT_FOUND;
        case SharedStatus::Busy: return KU_STATUS_WOULD_BLOCK;
        case SharedStatus::NotInitialized: return KU_STATUS_BAD_STATE;
    }
    return KU_STATUS_IO_ERROR;
}

ku_status_t event_status(ipc::event::Status status) {
    using EventStatus = ipc::event::Status;
    switch (status) {
        case EventStatus::Ok:
        case EventStatus::AlreadyInitialized: return KU_STATUS_OK;
        case EventStatus::InvalidArgument:
        case EventStatus::StaleHandle: return KU_STATUS_INVALID_ARGUMENT;
        case EventStatus::CapacityReached: return KU_STATUS_OUT_OF_MEMORY;
        case EventStatus::AccessDenied: return KU_STATUS_ACCESS_DENIED;
        case EventStatus::AlreadyGranted: return KU_STATUS_ALREADY_EXISTS;
        case EventStatus::WouldBlock: return KU_STATUS_WOULD_BLOCK;
        case EventStatus::NotInitialized: return KU_STATUS_BAD_STATE;
    }
    return KU_STATUS_IO_ERROR;
}

ku_status_t socket_status(net::socket::Status status) {
    using SocketStatus = net::socket::Status;
    switch (status) {
        case SocketStatus::Ok:
        case SocketStatus::AlreadyInitialized: return KU_STATUS_OK;
        case SocketStatus::InvalidArgument:
        case SocketStatus::StaleHandle: return KU_STATUS_INVALID_ARGUMENT;
        case SocketStatus::NotSupported: return KU_STATUS_NOT_SUPPORTED;
        case SocketStatus::CapacityReached: return KU_STATUS_OUT_OF_MEMORY;
        case SocketStatus::AccessDenied: return KU_STATUS_ACCESS_DENIED;
        case SocketStatus::AddressInUse: return KU_STATUS_ALREADY_EXISTS;
        case SocketStatus::WouldBlock: return KU_STATUS_WOULD_BLOCK;
        case SocketStatus::BufferTooSmall:
        case SocketStatus::PayloadTooLarge: return KU_STATUS_OUT_OF_RANGE;
        case SocketStatus::ConnectionRefused: return KU_STATUS_CONNECTION_REFUSED;
        case SocketStatus::ConnectionReset: return KU_STATUS_CONNECTION_RESET;
        case SocketStatus::TimedOut: return KU_STATUS_TIMED_OUT;
        case SocketStatus::NotInitialized:
        case SocketStatus::NotBound:
        case SocketStatus::NotConnected: return KU_STATUS_BAD_STATE;
        case SocketStatus::TransportError: return KU_STATUS_IO_ERROR;
    }
    return KU_STATUS_IO_ERROR;
}

net::Status socket_backend_send_udp(
    void*,
    const net::IPv4Address& destination,
    uint16_t source_port,
    uint16_t destination_port,
    const uint8_t* payload,
    size_t payload_length) {
    return net::service::socket_send_udp(
        destination, source_port, destination_port, payload, payload_length);
}

net::Status socket_backend_poll(void*, size_t budget, size_t* processed) {
    return net::service::poll(budget, processed);
}

net::Status socket_backend_take_udp(void*, net::UdpDatagram* datagram) {
    return net::service::socket_take_udp(datagram);
}

uint64_t socket_backend_monotonic_ms(void*) {
    if (!drivers::pit::initialized()) return 0U;
    const uint64_t frequency = drivers::pit::frequency_hz();
    if (frequency == 0U) return 0U;
    const uint64_t ticks = drivers::pit::ticks();
    const uint64_t seconds = ticks / frequency;
    const uint64_t remainder = ticks % frequency;
    if (seconds > UINT64_MAX / UINT64_C(1000)) return UINT64_MAX;
    return seconds * UINT64_C(1000) +
        (remainder * UINT64_C(1000)) / frequency;
}

net::Status socket_backend_tcp_begin_connect(
    void*,
    net::tcp_client::Client* client,
    const net::IPv4Address& destination,
    uint16_t source_port,
    uint16_t destination_port,
    uint32_t initial_sequence) {
    return net::service::socket_tcp_begin_connect(
        client, destination, source_port, destination_port, initial_sequence);
}

net::Status socket_backend_tcp_progress(void*, net::tcp_client::Client* client) {
    return net::service::socket_tcp_progress(client);
}

net::Status socket_backend_tcp_try_send(
    void*,
    net::tcp_client::Client* client,
    const uint8_t* data,
    size_t length,
    size_t* out_sent) {
    return net::service::socket_tcp_try_send(client, data, length, out_sent);
}

net::Status socket_backend_tcp_try_receive(
    void*,
    net::tcp_client::Client* client,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length) {
    return net::service::socket_tcp_try_receive(
        client, output, output_capacity, out_length);
}

net::Status socket_backend_tcp_begin_close(
    void*,
    net::tcp_client::Client* client) {
    return net::service::socket_tcp_begin_close(client);
}

bool copy_user_ipc_name(
    Context& context,
    uint64_t address,
    uint64_t length,
    char* output) {
    if (output == nullptr || length == 0U ||
        length > ipc::MAX_SERVICE_NAME || length > SIZE_MAX ||
        !validate_user_buffer(context, address, static_cast<size_t>(length))) {
        return false;
    }
    const auto* source = reinterpret_cast<const char*>(
        static_cast<uintptr_t>(address));
    for (size_t index = 0U; index < static_cast<size_t>(length); ++index) {
        if (source[index] == '\0') return false;
        output[index] = source[index];
    }
    output[static_cast<size_t>(length)] = '\0';
    return true;
}

bool extended_syscall_number(uint64_t number) {
    return (number >= KU_SYS_IPC_BIND && number <= KU_SYS_IPC_CLOSE) ||
        number == KU_SYS_IPC_QUERY ||
        (number >= KU_SYS_SHM_CREATE && number <= KU_SYS_SHM_CLOSE) ||
        (number >= KU_SYS_EVENT_CREATE && number <= KU_SYS_EVENT_CLOSE) ||
        (number >= KU_SYS_SOCKET_CREATE && number <= KU_SYS_SOCKET_POLL) ||
        (number >= KU_SYS_DEVICE_ENUMERATE && number <= KU_SYS_DEVICE_RESOURCE);
}

Context* registered_context_for_current_thread() {
    const threading::ThreadId tid = threading::current();
    const uint64_t root =
        memory::kernel_virtual_memory::active_root_table_physical();
    for (Context* context : g_contexts) {
        if (context != nullptr && context->tid == tid &&
            context->address_space.address_space.root_table_physical == root) {
            return context;
        }
    }
    return nullptr;
}

RuntimeSharedMapping* find_shared_mapping(
    Context& context,
    ipc::shared_memory::Handle handle) {
    for (RuntimeSharedMapping& mapping : g_shared_mappings) {
        if (mapping.active && mapping.context == &context &&
            mapping.handle == handle) return &mapping;
    }
    return nullptr;
}

RuntimeSharedMapping* reserve_shared_mapping_record(Context& context) {
    size_t active_for_context = 0U;
    for (RuntimeSharedMapping& mapping : g_shared_mappings) {
        if (mapping.active && mapping.context == &context) ++active_for_context;
    }
    if (active_for_context >= kMaximumSharedMappingsPerContext) return nullptr;
    for (RuntimeSharedMapping& mapping : g_shared_mappings) {
        if (!mapping.active) return &mapping;
    }
    return nullptr;
}

bool shared_slot_available(Context& context, uint64_t base, size_t page_count) {
    for (size_t page = 0U; page < page_count; ++page) {
        memory::virtual_memory::Mapping existing{};
        const memory::virtual_memory::Status status =
            memory::virtual_memory::query_page(
                &context.address_space.address_space,
                base + static_cast<uint64_t>(page) * memory::virtual_memory::PAGE_SIZE,
                &existing);
        if (status != memory::virtual_memory::Status::NotMapped) return false;
    }
    return true;
}

uint64_t find_shared_mapping_base(Context& context, size_t page_count) {
    for (size_t slot = 0U; slot < kMaximumSharedMappingsPerContext; ++slot) {
        const uint64_t base = kSharedRegionBase +
            static_cast<uint64_t>(slot) * kSharedMappingStride;
        if (shared_slot_available(context, base, page_count)) return base;
    }
    return 0U;
}

ku_result_t map_shared_object(
    Context& context,
    ipc::shared_memory::Handle handle) {
    if (find_shared_mapping(context, handle) != nullptr) {
        return KU_STATUS_ALREADY_EXISTS;
    }
    RuntimeSharedMapping* record = reserve_shared_mapping_record(context);
    if (record == nullptr) return KU_STATUS_OUT_OF_MEMORY;

    ipc::shared_memory::View view{};
    const uint64_t interrupt_flags = save_and_disable_interrupts();
    const ipc::shared_memory::Status acquire_status =
        ipc::shared_memory::acquire(context.pid, handle, &view);
    restore_interrupts(interrupt_flags);
    if (acquire_status != ipc::shared_memory::Status::Ok) {
        return shared_memory_status(acquire_status);
    }

    const uint64_t base = find_shared_mapping_base(context, view.page_count);
    if (base == 0U) {
        const uint64_t flags = save_and_disable_interrupts();
        static_cast<void>(ipc::shared_memory::release(context.pid, handle));
        restore_interrupts(flags);
        return KU_STATUS_OUT_OF_MEMORY;
    }

    size_t mapped = 0U;
    for (; mapped < view.page_count; ++mapped) {
        const memory::virtual_memory::Status map_status =
            memory::virtual_memory::map_page(
                &context.address_space.address_space,
                base + static_cast<uint64_t>(mapped) * memory::virtual_memory::PAGE_SIZE,
                static_cast<uint64_t>(reinterpret_cast<uintptr_t>(view.frames[mapped])),
                memory::virtual_memory::MapFlags::User |
                    memory::virtual_memory::MapFlags::Writable |
                    memory::virtual_memory::MapFlags::NoExecute);
        if (map_status != memory::virtual_memory::Status::Ok) break;
    }
    if (mapped != view.page_count) {
        while (mapped != 0U) {
            --mapped;
            static_cast<void>(memory::virtual_memory::unmap_page(
                &context.address_space.address_space,
                base + static_cast<uint64_t>(mapped) * memory::virtual_memory::PAGE_SIZE));
        }
        const uint64_t flags = save_and_disable_interrupts();
        static_cast<void>(ipc::shared_memory::release(context.pid, handle));
        restore_interrupts(flags);
        return KU_STATUS_OUT_OF_MEMORY;
    }

    *record = {&context, handle, base, view.page_count, true};
    return static_cast<ku_result_t>(base);
}

ku_status_t unmap_shared_object(
    Context& context,
    ipc::shared_memory::Handle handle) {
    RuntimeSharedMapping* mapping = find_shared_mapping(context, handle);
    if (mapping == nullptr) return KU_STATUS_NOT_FOUND;
    for (size_t page = 0U; page < mapping->page_count; ++page) {
        if (memory::virtual_memory::unmap_page(
                &context.address_space.address_space,
                mapping->base + static_cast<uint64_t>(page) *
                    memory::virtual_memory::PAGE_SIZE) !=
            memory::virtual_memory::Status::Ok) {
            return KU_STATUS_IO_ERROR;
        }
    }
    const uint64_t interrupt_flags = save_and_disable_interrupts();
    const ipc::shared_memory::Status status =
        ipc::shared_memory::release(context.pid, handle);
    restore_interrupts(interrupt_flags);
    *mapping = {};
    return shared_memory_status(status);
}

void cleanup_shared_mappings(Context& context) {
    for (RuntimeSharedMapping& mapping : g_shared_mappings) {
        if (!mapping.active || mapping.context != &context) continue;
        for (size_t page = 0U; page < mapping.page_count; ++page) {
            static_cast<void>(memory::virtual_memory::unmap_page(
                &context.address_space.address_space,
                mapping.base + static_cast<uint64_t>(page) *
                    memory::virtual_memory::PAGE_SIZE));
        }
        const uint64_t interrupt_flags = save_and_disable_interrupts();
        static_cast<void>(ipc::shared_memory::release(context.pid, mapping.handle));
        restore_interrupts(interrupt_flags);
        mapping = {};
    }
}

void extended_syscall_handler(
    arch::x86_64::interrupts::InterruptFrame& frame) {
    // Context::active describes whether the userspace image may continue
    // executing; registration lasts until cleanup unregisters the Context.
    // A late saved Ring-3 frame can therefore arrive after SYS_EXIT marked the
    // image inactive but before the owning kernel thread has fully unwound.
    // Resolve that registered owner by the scheduler identity and CR3 and
    // deterministically return it to the kernel instead of feeding a BAD_STATE
    // result back into an already-terminated userspace image.
    if ((frame.cs & 3U) == 3U) {
        Context* registered = registered_context_for_current_thread();
        if (registered != nullptr && !registered->active) {
            finish_from_interrupt(*registered, frame, registered->result.exit_code);
            return;
        }
    }

    if (!extended_syscall_number(frame.rax) && frame.rax != KU_SYS_EXIT) {
        syscall_handler(frame);
        return;
    }

    Context* context = current_context();
    if (context == nullptr || (frame.cs & 3U) != 3U) {
        syscall_handler(frame);
        return;
    }

    if (frame.rax == KU_SYS_EXIT) {
        cleanup_shared_mappings(*context);
        const uint64_t interrupt_flags = save_and_disable_interrupts();
        ipc::release_process(context->pid);
        ipc::shared_memory::release_process(context->pid);
        ipc::event::release_process(context->pid);
        net::socket::release_process(context->pid);
        restore_interrupts(interrupt_flags);
        syscall_handler(frame);
        return;
    }

    if ((frame.cs & UINT64_C(0xFFFF)) == arch::x86_64::gdt::USER_CODE_SELECTOR &&
        (frame.ss & UINT64_C(0xFFFF)) == arch::x86_64::gdt::USER_DATA_SELECTOR) {
        context->result.entered_ring3 = true;
    }

    switch (frame.rax) {
        case KU_SYS_DEVICE_ENUMERATE: {
            if (frame.rdx != 0U || frame.rdi > SIZE_MAX ||
                !validate_user_buffer(*context, frame.rsi, sizeof(ku_device_handle_t), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* output = reinterpret_cast<ku_device_handle_t*>(
                static_cast<uintptr_t>(frame.rsi));
            *output = KU_DEVICE_INVALID_HANDLE;
            const size_t requested = static_cast<size_t>(frame.rdi);
            size_t active_index = 0U;
            for (drivers::device::DeviceId id = 0U;
                 id < drivers::device::count(); ++id) {
                const drivers::device::Device* device = drivers::device::get(id);
                if (device == nullptr) continue;
                if (active_index == requested) {
                    *output = drivers::device::handle_for(id);
                    frame.rax = *output != KU_DEVICE_INVALID_HANDLE
                        ? static_cast<uint64_t>(KU_STATUS_OK)
                        : static_cast<uint64_t>(KU_STATUS_BAD_STATE);
                    return;
                }
                ++active_index;
            }
            frame.rax = static_cast<uint64_t>(KU_STATUS_END_OF_STREAM);
            return;
        }
        case KU_SYS_DEVICE_QUERY: {
            if (frame.rdi == KU_DEVICE_INVALID_HANDLE ||
                frame.rdx != sizeof(ku_device_info) ||
                !validate_user_buffer(*context, frame.rsi, sizeof(ku_device_info), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* output = reinterpret_cast<ku_device_info*>(
                static_cast<uintptr_t>(frame.rsi));
            if (output->structure_size != sizeof(*output) ||
                output->version != KU_DEVICE_INFO_VERSION) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_VERSION_MISMATCH);
                return;
            }
            const auto handle = static_cast<drivers::device::DeviceHandle>(frame.rdi);
            const drivers::device::Device* device = drivers::device::resolve(handle);
            if (device == nullptr) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_NOT_FOUND);
                return;
            }
            ku_device_info result{};
            result.structure_size = sizeof(result);
            result.version = KU_DEVICE_INFO_VERSION;
            result.handle = handle;
            result.parent = device->parent == drivers::device::INVALID_DEVICE_ID
                ? KU_DEVICE_INVALID_HANDLE
                : drivers::device::handle_for(device->parent);
            result.capabilities = device->capabilities;
            result.lifecycle_generation = device->lifecycle_generation;
            result.type = static_cast<uint32_t>(device->type);
            result.bus = static_cast<uint32_t>(device->bus);
            result.state = static_cast<uint32_t>(device->status);
            result.resource_count = static_cast<uint32_t>(device->resource_count);
            result.child_count = static_cast<uint32_t>(device->child_count);
            result.vendor_id = device->vendor_id;
            result.device_id = device->device_id;
            result.class_code = device->class_code;
            result.subclass = device->subclass;
            result.programming_interface = device->programming_interface;
            result.pci_segment = device->bus_address.segment;
            result.pci_bus = device->bus_address.bus;
            result.pci_slot = device->bus_address.slot;
            result.pci_function = device->bus_address.function;
            for (size_t index = 0U; index < KU_DEVICE_NAME_CAPACITY; ++index) {
                result.name[index] = device->name[index];
                if (device->name[index] == '\0') break;
            }
            for (size_t index = 0U; index < KU_DEVICE_DRIVER_NAME_CAPACITY; ++index) {
                result.driver[index] = device->driver_name[index];
                if (device->driver_name[index] == '\0') break;
            }
            *output = result;
            frame.rax = static_cast<uint64_t>(KU_STATUS_OK);
            return;
        }
        case KU_SYS_DEVICE_RESOURCE: {
            if (frame.rdi == KU_DEVICE_INVALID_HANDLE || frame.rsi > SIZE_MAX ||
                !validate_user_buffer(
                    *context, frame.rdx, sizeof(ku_device_resource), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* output = reinterpret_cast<ku_device_resource*>(
                static_cast<uintptr_t>(frame.rdx));
            if (output->structure_size != sizeof(*output) || output->reserved != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            drivers::device::Resource resource{};
            const KStatus status = drivers::device::get_resource(
                static_cast<drivers::device::DeviceHandle>(frame.rdi),
                static_cast<size_t>(frame.rsi),
                &resource);
            if (status == KStatus::NotFound) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_NOT_FOUND);
                return;
            }
            if (status == KStatus::OutOfRange) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_OUT_OF_RANGE);
                return;
            }
            if (status != KStatus::Ok) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_BAD_STATE);
                return;
            }
            output->type = static_cast<uint32_t>(resource.type);
            output->start = resource.start;
            output->length = resource.length;
            output->flags = resource.flags;
            frame.rax = static_cast<uint64_t>(KU_STATUS_OK);
            return;
        }
        case KU_SYS_SOCKET_CREATE: {
            if (frame.rdx != KU_SOCKET_FLAG_NONE) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            net::socket::Type type;
            if (frame.rdi == KU_SOCKET_DATAGRAM) {
                type = net::socket::Type::Datagram;
            } else if (frame.rdi == KU_SOCKET_STREAM) {
                type = net::socket::Type::Stream;
            } else {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            net::socket::Protocol protocol;
            if (frame.rsi == KU_SOCKET_PROTOCOL_UDP) {
                protocol = net::socket::Protocol::Udp;
            } else if (frame.rsi == KU_SOCKET_PROTOCOL_TCP) {
                protocol = net::socket::Protocol::Tcp;
            } else {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            net::socket::Handle handle = net::socket::INVALID_HANDLE;
            const net::socket::Status status = net::socket::create(
                context->pid, type, protocol, &handle);
            frame.rax = status == net::socket::Status::Ok
                ? handle : static_cast<uint64_t>(socket_status(status));
            return;
        }
        case KU_SYS_SOCKET_BIND:
        case KU_SYS_SOCKET_CONNECT: {
            if (frame.rdx != sizeof(ku_ipv4_endpoint) ||
                !validate_user_buffer(*context, frame.rsi, sizeof(ku_ipv4_endpoint))) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            const auto* endpoint = reinterpret_cast<const ku_ipv4_endpoint*>(
                static_cast<uintptr_t>(frame.rsi));
            if (endpoint->reserved != 0U || endpoint->port == 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            net::socket::Endpoint target{};
            for (size_t index = 0U; index < 4U; ++index) {
                target.address.bytes[index] = endpoint->address[index];
            }
            target.port = endpoint->port;
            const net::socket::Status status = frame.rax == KU_SYS_SOCKET_BIND
                ? net::socket::bind(
                    context->pid, static_cast<net::socket::Handle>(frame.rdi), target)
                : net::socket::connect(
                    context->pid, static_cast<net::socket::Handle>(frame.rdi), target);
            frame.rax = static_cast<uint64_t>(socket_status(status));
            return;
        }
        case KU_SYS_SOCKET_SEND: {
            if (frame.rdx == 0U || frame.rdx > SIZE_MAX ||
                !validate_user_buffer(*context, frame.rsi, static_cast<size_t>(frame.rdx))) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            size_t sent = 0U;
            const net::socket::Status status = net::socket::send(
                context->pid,
                static_cast<net::socket::Handle>(frame.rdi),
                reinterpret_cast<const void*>(static_cast<uintptr_t>(frame.rsi)),
                static_cast<size_t>(frame.rdx),
                &sent);
            frame.rax = status == net::socket::Status::Ok
                ? static_cast<uint64_t>(sent)
                : static_cast<uint64_t>(socket_status(status));
            return;
        }
        case KU_SYS_SOCKET_RECEIVE: {
            if (frame.rsi != sizeof(ku_socket_receive_request) || frame.rdx != 0U ||
                !validate_user_buffer(
                    *context, frame.rdi, sizeof(ku_socket_receive_request), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* request = reinterpret_cast<ku_socket_receive_request*>(
                static_cast<uintptr_t>(frame.rdi));
            if (request->structure_size != sizeof(*request) ||
                request->flags != KU_SOCKET_FLAG_NONE ||
                request->buffer == nullptr || request->buffer_capacity == 0U ||
                request->buffer_capacity > SIZE_MAX ||
                !validate_user_buffer(
                    *context,
                    static_cast<uint64_t>(reinterpret_cast<uintptr_t>(request->buffer)),
                    static_cast<size_t>(request->buffer_capacity),
                    true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            size_t received = 0U;
            net::socket::Endpoint source{};
            const net::socket::Status status = net::socket::receive(
                context->pid,
                static_cast<net::socket::Handle>(request->socket),
                request->buffer,
                static_cast<size_t>(request->buffer_capacity),
                &received,
                &source);
            request->bytes_received = received;
            request->source = {};
            if (status == net::socket::Status::Ok) {
                for (size_t index = 0U; index < 4U; ++index) {
                    request->source.address[index] = source.address.bytes[index];
                }
                request->source.port = source.port;
            }
            frame.rax = static_cast<uint64_t>(socket_status(status));
            return;
        }
        case KU_SYS_SOCKET_CLOSE: {
            if (frame.rsi != 0U || frame.rdx != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            frame.rax = static_cast<uint64_t>(socket_status(net::socket::close(
                context->pid, static_cast<net::socket::Handle>(frame.rdi))));
            return;
        }
        case KU_SYS_SOCKET_POLL: {
            if (frame.rsi == 0U ||
                (frame.rsi & ~static_cast<uint64_t>(KU_SOCKET_READY_ALL)) != 0U ||
                !validate_user_buffer(*context, frame.rdx, sizeof(uint32_t), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* ready = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(frame.rdx));
            uint32_t ready_value = 0U;
            const net::socket::Status status = net::socket::readiness(
                context->pid,
                static_cast<net::socket::Handle>(frame.rdi),
                static_cast<uint32_t>(frame.rsi),
                &ready_value);
            if (status == net::socket::Status::Ok) *ready = ready_value;
            frame.rax = static_cast<uint64_t>(socket_status(status));
            return;
        }
        case KU_SYS_IPC_QUERY: {
            char name[ipc::MAX_SERVICE_NAME + 1U]{};
            if (!copy_user_ipc_name(*context, frame.rdi, frame.rsi, name) ||
                frame.rdx == 0U ||
                !validate_user_buffer(*context, frame.rdx, sizeof(ku_service_info), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* user_info = reinterpret_cast<ku_service_info*>(
                static_cast<uintptr_t>(frame.rdx));
            if (user_info->structure_size != sizeof(*user_info) ||
                user_info->abi_version != KU_SERVICE_INFO_ABI_VERSION) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_VERSION_MISMATCH);
                return;
            }
            ipc::ServiceInfo info{};
            const uint64_t interrupt_flags = save_and_disable_interrupts();
            const ipc::Status status = ipc::query(
                name, static_cast<size_t>(frame.rsi), &info);
            restore_interrupts(interrupt_flags);
            if (status == ipc::Status::Ok) {
                user_info->service_version = info.service_version;
                user_info->minimum_client_version = info.minimum_client_version;
                user_info->capabilities = info.capabilities;
                user_info->owner_pid = info.owner_pid;
            }
            frame.rax = static_cast<uint64_t>(ipc_status(status));
            return;
        }
        case KU_SYS_IPC_BIND:
        case KU_SYS_IPC_CONNECT: {
            char name[ipc::MAX_SERVICE_NAME + 1U]{};
            if (!copy_user_ipc_name(*context, frame.rdi, frame.rsi, name)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }

            ipc::ServiceMetadata metadata{};
            const ipc::ServiceMetadata* metadata_ptr = nullptr;
            ipc::ServiceNegotiation negotiation{};
            ipc::ServiceNegotiation* negotiation_ptr = nullptr;
            ku_service_negotiation* user_negotiation = nullptr;

            if (frame.rdx != 0U && frame.rax == KU_SYS_IPC_BIND) {
                if (!validate_user_buffer(
                        *context, frame.rdx, sizeof(ku_service_descriptor))) {
                    frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                    return;
                }
                const auto* descriptor = reinterpret_cast<const ku_service_descriptor*>(
                    static_cast<uintptr_t>(frame.rdx));
                if (descriptor->structure_size != sizeof(*descriptor) ||
                    descriptor->abi_version != KU_SERVICE_DESCRIPTOR_ABI_VERSION) {
                    frame.rax = static_cast<uint64_t>(KU_STATUS_VERSION_MISMATCH);
                    return;
                }
                if (descriptor->reserved != 0U || descriptor->service_version == 0U ||
                    descriptor->minimum_client_version == 0U ||
                    descriptor->minimum_client_version > descriptor->service_version) {
                    frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                    return;
                }
                metadata.service_version = descriptor->service_version;
                metadata.minimum_client_version = descriptor->minimum_client_version;
                metadata.capabilities = descriptor->capabilities;
                metadata_ptr = &metadata;
            } else if (frame.rdx != 0U && frame.rax == KU_SYS_IPC_CONNECT) {
                if (!validate_user_buffer(
                        *context, frame.rdx, sizeof(ku_service_negotiation), true)) {
                    frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                    return;
                }
                user_negotiation = reinterpret_cast<ku_service_negotiation*>(
                    static_cast<uintptr_t>(frame.rdx));
                if (user_negotiation->structure_size != sizeof(*user_negotiation) ||
                    user_negotiation->abi_version != KU_SERVICE_NEGOTIATION_ABI_VERSION) {
                    frame.rax = static_cast<uint64_t>(KU_STATUS_VERSION_MISMATCH);
                    return;
                }
                if (user_negotiation->reserved != 0U ||
                    user_negotiation->minimum_version == 0U ||
                    user_negotiation->maximum_version < user_negotiation->minimum_version) {
                    frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                    return;
                }
                negotiation.minimum_version = user_negotiation->minimum_version;
                negotiation.maximum_version = user_negotiation->maximum_version;
                negotiation_ptr = &negotiation;
            }

            ipc::Handle handle = ipc::INVALID_HANDLE;
            const uint64_t interrupt_flags = save_and_disable_interrupts();
            const ipc::Status status = frame.rax == KU_SYS_IPC_BIND
                ? ipc::bind(
                    context->pid, name, static_cast<size_t>(frame.rsi), &handle,
                    metadata_ptr)
                : ipc::connect(
                    context->pid, name, static_cast<size_t>(frame.rsi), &handle,
                    negotiation_ptr);
            restore_interrupts(interrupt_flags);
            if (status == ipc::Status::Ok && user_negotiation != nullptr) {
                user_negotiation->selected_version = negotiation.selected_version;
                user_negotiation->service_version = negotiation.service_version;
                user_negotiation->minimum_client_version =
                    negotiation.minimum_client_version;
                user_negotiation->capabilities = negotiation.capabilities;
                user_negotiation->owner_pid = negotiation.owner_pid;
            }
            frame.rax = status == ipc::Status::Ok
                ? handle : static_cast<uint64_t>(ipc_status(status));
            return;
        }
        case KU_SYS_IPC_ACCEPT: {
            if (frame.rsi != 0U || frame.rdx != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            ipc::Handle handle = ipc::INVALID_HANDLE;
            const uint64_t interrupt_flags = save_and_disable_interrupts();
            const ipc::Status status = ipc::accept(
                context->pid, static_cast<ipc::Handle>(frame.rdi), &handle);
            restore_interrupts(interrupt_flags);
            frame.rax = status == ipc::Status::Ok
                ? handle : static_cast<uint64_t>(ipc_status(status));
            return;
        }
        case KU_SYS_IPC_SEND: {
            if (frame.rdx > ipc::MAX_MESSAGE_SIZE || frame.rdx > SIZE_MAX) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_OUT_OF_RANGE);
                return;
            }
            const size_t size = static_cast<size_t>(frame.rdx);
            if (!validate_user_buffer(*context, frame.rsi, size)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            const void* data = size == 0U
                ? nullptr
                : reinterpret_cast<const void*>(static_cast<uintptr_t>(frame.rsi));
            const uint64_t interrupt_flags = save_and_disable_interrupts();
            const ipc::Status status = ipc::send(
                context->pid,
                static_cast<ipc::Handle>(frame.rdi),
                data,
                size);
            restore_interrupts(interrupt_flags);
            frame.rax = static_cast<uint64_t>(ipc_status(status));
            return;
        }
        case KU_SYS_IPC_RECEIVE: {
            if (frame.rdx != sizeof(ku_ipc_message) ||
                !validate_user_buffer(
                    *context, frame.rsi, sizeof(ku_ipc_message), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* output = reinterpret_cast<ku_ipc_message*>(
                static_cast<uintptr_t>(frame.rsi));
            if (output->structure_size != sizeof(*output)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_VERSION_MISMATCH);
                return;
            }
            ipc::Message message{};
            const uint64_t interrupt_flags = save_and_disable_interrupts();
            const ipc::Status status = ipc::receive(
                context->pid,
                static_cast<ipc::Handle>(frame.rdi),
                &message);
            restore_interrupts(interrupt_flags);
            if (status == ipc::Status::Ok) {
                *output = {};
                output->structure_size = sizeof(*output);
                output->data_size = static_cast<uint32_t>(message.size);
                output->sender_pid = message.sender_pid;
                for (size_t index = 0U; index < message.size; ++index) {
                    output->data[index] = message.bytes[index];
                }
            }
            frame.rax = static_cast<uint64_t>(ipc_status(status));
            return;
        }
        case KU_SYS_IPC_CLOSE: {
            if (frame.rsi != 0U || frame.rdx != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            const uint64_t interrupt_flags = save_and_disable_interrupts();
            const ipc::Status status = ipc::close(
                context->pid, static_cast<ipc::Handle>(frame.rdi));
            restore_interrupts(interrupt_flags);
            frame.rax = static_cast<uint64_t>(ipc_status(status));
            return;
        }
        case KU_SYS_SHM_CREATE: {
            if (frame.rsi != 0U || frame.rdx != 0U || frame.rdi == 0U ||
                frame.rdi > ipc::shared_memory::MAX_PAGES_PER_OBJECT *
                    ipc::shared_memory::PAGE_SIZE || frame.rdi > SIZE_MAX) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_OUT_OF_RANGE);
                return;
            }
            ipc::shared_memory::Handle handle = ipc::shared_memory::INVALID_HANDLE;
            const uint64_t interrupt_flags = save_and_disable_interrupts();
            const ipc::shared_memory::Status status = ipc::shared_memory::create(
                context->pid, static_cast<size_t>(frame.rdi), &handle);
            restore_interrupts(interrupt_flags);
            frame.rax = status == ipc::shared_memory::Status::Ok
                ? handle : static_cast<uint64_t>(shared_memory_status(status));
            return;
        }
        case KU_SYS_SHM_GRANT: {
            if (frame.rdx != 0U || frame.rsi == 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            const uint64_t interrupt_flags = save_and_disable_interrupts();
            const ipc::shared_memory::Status status = ipc::shared_memory::grant(
                context->pid,
                static_cast<ipc::shared_memory::Handle>(frame.rdi),
                static_cast<ipc::shared_memory::ProcessId>(frame.rsi));
            restore_interrupts(interrupt_flags);
            frame.rax = static_cast<uint64_t>(shared_memory_status(status));
            return;
        }
        case KU_SYS_SHM_MAP: {
            if (frame.rsi != 0U || frame.rdx != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            frame.rax = static_cast<uint64_t>(map_shared_object(
                *context, static_cast<ipc::shared_memory::Handle>(frame.rdi)));
            return;
        }
        case KU_SYS_SHM_UNMAP: {
            if (frame.rsi != 0U || frame.rdx != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            frame.rax = static_cast<uint64_t>(unmap_shared_object(
                *context, static_cast<ipc::shared_memory::Handle>(frame.rdi)));
            return;
        }
        case KU_SYS_SHM_CLOSE: {
            if (frame.rsi != 0U || frame.rdx != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            const uint64_t interrupt_flags = save_and_disable_interrupts();
            const ipc::shared_memory::Status status = ipc::shared_memory::close(
                context->pid,
                static_cast<ipc::shared_memory::Handle>(frame.rdi));
            restore_interrupts(interrupt_flags);
            frame.rax = static_cast<uint64_t>(shared_memory_status(status));
            return;
        }
        case KU_SYS_EVENT_CREATE: {
            if (frame.rdx != 0U || frame.rdi > KU_EVENT_MANUAL_RESET ||
                frame.rsi > 1U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            ipc::event::Handle handle = ipc::event::INVALID_HANDLE;
            const uint64_t interrupt_flags = save_and_disable_interrupts();
            const ipc::event::Status status = ipc::event::create(
                context->pid,
                frame.rdi == KU_EVENT_MANUAL_RESET
                    ? ipc::event::ResetMode::Manual : ipc::event::ResetMode::Auto,
                frame.rsi != 0U,
                &handle);
            restore_interrupts(interrupt_flags);
            frame.rax = status == ipc::event::Status::Ok
                ? handle : static_cast<uint64_t>(event_status(status));
            return;
        }
        case KU_SYS_EVENT_GRANT: {
            if (frame.rdx != 0U || frame.rsi == 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            const uint64_t interrupt_flags = save_and_disable_interrupts();
            const ipc::event::Status status = ipc::event::grant(
                context->pid,
                static_cast<ipc::event::Handle>(frame.rdi),
                static_cast<ipc::event::ProcessId>(frame.rsi));
            restore_interrupts(interrupt_flags);
            frame.rax = static_cast<uint64_t>(event_status(status));
            return;
        }
        case KU_SYS_EVENT_SIGNAL:
        case KU_SYS_EVENT_RESET:
        case KU_SYS_EVENT_POLL:
        case KU_SYS_EVENT_CLOSE: {
            if (frame.rsi != 0U || frame.rdx != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            const ipc::event::Handle handle =
                static_cast<ipc::event::Handle>(frame.rdi);
            const uint64_t interrupt_flags = save_and_disable_interrupts();
            ipc::event::Status status = ipc::event::Status::InvalidArgument;
            if (frame.rax == KU_SYS_EVENT_SIGNAL) {
                status = ipc::event::signal(context->pid, handle);
            } else if (frame.rax == KU_SYS_EVENT_RESET) {
                status = ipc::event::reset(context->pid, handle);
            } else if (frame.rax == KU_SYS_EVENT_POLL) {
                status = ipc::event::poll(context->pid, handle);
            } else {
                status = ipc::event::close(context->pid, handle);
            }
            restore_interrupts(interrupt_flags);
            frame.rax = static_cast<uint64_t>(event_status(status));
            return;
        }
        default:
            syscall_handler(frame);
            return;
    }
}

} // namespace

Status initialize() {
    const ipc::Status ipc_initialize_status = ipc::initialize();
    if (ipc_initialize_status != ipc::Status::Ok &&
        ipc_initialize_status != ipc::Status::AlreadyInitialized) {
        return Status::InterruptRegistrationFailed;
    }
    const ipc::shared_memory::Status shared_initialize_status =
        ipc::shared_memory::initialize();
    if (shared_initialize_status != ipc::shared_memory::Status::Ok &&
        shared_initialize_status != ipc::shared_memory::Status::AlreadyInitialized) {
        return Status::InterruptRegistrationFailed;
    }
    const ipc::event::Status event_initialize_status = ipc::event::initialize();
    if (event_initialize_status != ipc::event::Status::Ok &&
        event_initialize_status != ipc::event::Status::AlreadyInitialized) {
        return Status::InterruptRegistrationFailed;
    }
    const net::socket::Backend socket_backend{
        nullptr,
        socket_backend_send_udp,
        socket_backend_poll,
        socket_backend_take_udp,
        socket_backend_monotonic_ms,
        socket_backend_tcp_begin_connect,
        socket_backend_tcp_progress,
        socket_backend_tcp_try_send,
        socket_backend_tcp_try_receive,
        socket_backend_tcp_begin_close,
    };
    const net::socket::Status socket_initialize_status =
        net::socket::initialize(socket_backend);
    if (socket_initialize_status != net::socket::Status::Ok &&
        socket_initialize_status != net::socket::Status::AlreadyInitialized) {
        return Status::InterruptRegistrationFailed;
    }
    for (RuntimeSharedMapping& mapping : g_shared_mappings) mapping = {};

    const Status status = legacy_initialize();
    if (status != Status::Ok) return status;

    arch::x86_64::interrupts::unregister_handler(kSyscallVector);
    if (!arch::x86_64::interrupts::register_handler(
            kSyscallVector, extended_syscall_handler) ||
        !arch::x86_64::interrupts::set_gate_privilege(
            kSyscallVector, arch::x86_64::interrupts::GatePrivilege::User) ||
        !arch::x86_64::interrupts::set_gate_type(
            kSyscallVector, arch::x86_64::interrupts::GateType::Trap)) {
        arch::x86_64::interrupts::unregister_handler(kSyscallVector);
        g_initialized = false;
        return Status::InterruptRegistrationFailed;
    }
    return Status::Ok;
}

} // namespace user::runtime
