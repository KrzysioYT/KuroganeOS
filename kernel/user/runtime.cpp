#include "runtime.hpp"

#include <kurogane/audio.h>
#include <kurogane/desktop.h>
#include <kurogane/filesystem.h>
#include <kurogane/ipc.h>
#include <kurogane/network.h>
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
#include "../fs/root_volume.hpp"
#include "../ipc/channel.hpp"
#include "../memory/allocator.hpp"
#include "../memory/kernel_virtual_memory.hpp"
#include "../memory/physical_memory.hpp"
#include "../net/service.hpp"
#include "../task/process.hpp"
#include "../task/thread.hpp"
#include "../terminal.hpp"
#include "../drivers/framebuffer.hpp"
#include "../ui/ui.hpp"
#include "../ui/window_manager.hpp"

// Keep the already-qualified runtime implementation byte-for-byte and extend
// only its public initialize entry point and syscall handler. All headers are
// pre-included above so renaming the single implementation-level initialize()
// token does not affect declarations from included headers.
#define initialize legacy_initialize
#include "runtime_base.inc"
#undef initialize

namespace user::runtime {
namespace {

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

bool ipc_syscall_number(uint64_t number) {
    return number >= KU_SYS_IPC_BIND && number <= KU_SYS_IPC_CLOSE;
}

void extended_syscall_handler(
    arch::x86_64::interrupts::InterruptFrame& frame) {
    if (!ipc_syscall_number(frame.rax) && frame.rax != KU_SYS_EXIT) {
        syscall_handler(frame);
        return;
    }

    Context* context = current_context();
    if (context == nullptr || (frame.cs & 3U) != 3U) {
        syscall_handler(frame);
        return;
    }

    if (frame.rax == KU_SYS_EXIT) {
        const uint64_t interrupt_flags = save_and_disable_interrupts();
        ipc::release_process(context->pid);
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
