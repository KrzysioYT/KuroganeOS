#include "runtime.hpp"

#include <kurogane/audio.h>
#include <kurogane/desktop.h>
#include <kurogane/event.h>
#include <kurogane/filesystem.h>
#include <kurogane/ipc.h>
#include <kurogane/network.h>
#include <kurogane/shared_memory.h>
#include <kurogane/status.h>
#include <kurogane/syscall.h>
#include <kurogane/system.h>
#include <kurogane/ui.h>

#include "elf_loader.hpp"
#include "console.hpp"
#include "ui_window_adapter.hpp"
#include "../arch/x86_64/gdt.hpp"
#include "../core/log.hpp"
#include "../core/system_metrics.hpp"
#include "../drivers/audio/ac97.hpp"
#include "../fs/root_volume.hpp"
#include "../ipc/channel.hpp"
#include "../ipc/event.hpp"
#include "../ipc/shared_memory.hpp"
#include "../memory/allocator.hpp"
#include "../memory/kernel_virtual_memory.hpp"
#include "../memory/physical_memory.hpp"
#include "../net/service.hpp"
#include "../task/process.hpp"
#include "../task/thread.hpp"
#include "../terminal.hpp"
#include "../drivers/framebuffer.hpp"
#include "../ui/forged_surface.hpp"
#include "../ui/ui.hpp"
#include "../ui/window_manager.hpp"

#define initialize legacy_initialize
#define create_window create_ring3_window
#define native_surface forged_surface
#include "runtime_base.inc"
#undef native_surface
#undef create_window
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
        (number >= KU_SYS_SHM_CREATE && number <= KU_SYS_SHM_CLOSE) ||
        (number >= KU_SYS_EVENT_CREATE && number <= KU_SYS_EVENT_CLOSE);
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
        restore_interrupts(interrupt_flags);
        syscall_handler(frame);
        return;
    }

    if ((frame.cs & UINT64_C(0xFFFF)) == arch::x86_64::gdt::USER_CODE_SELECTOR &&
        (frame.ss & UINT64_C(0xFFFF)) == arch::x86_64::gdt::USER_DATA_SELECTOR) {
        context->result.entered_ring3 = true;
    }

    switch (frame.rax) {
        case KU_SYS_IPC_BIND:
        case KU_SYS_IPC_CONNECT: {
            if (frame.rdx != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            char name[ipc::MAX_SERVICE_NAME + 1U]{};
            if (!copy_user_ipc_name(*context, frame.rdi, frame.rsi, name)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            ipc::Handle handle = ipc::INVALID_HANDLE;
            const uint64_t interrupt_flags = save_and_disable_interrupts();
            const ipc::Status status = frame.rax == KU_SYS_IPC_BIND
                ? ipc::bind(
                    context->pid, name, static_cast<size_t>(frame.rsi), &handle)
                : ipc::connect(
                    context->pid, name, static_cast<size_t>(frame.rsi), &handle);
            restore_interrupts(interrupt_flags);
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
