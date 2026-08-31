#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:80]!r}")
    file.write_text(text.replace(old, new, 1))


def require_absent(path: str) -> None:
    if Path(path).exists():
        raise SystemExit(f"{path}: file already exists")


# ---------------------------------------------------------------------------
# Scheduler: a blocked Ring-3 thread must return control to the kernel loop,
# and readiness completion must only rewrite the saved syscall return value.
# ---------------------------------------------------------------------------
replace_once(
    "kernel/task/thread.hpp",
    "using Entry = void (*)(void* argument);\n",
    "using Entry = void (*)(void* argument);\n"
    "using PreDispatchHook = void (*)();\n",
)
replace_once(
    "kernel/task/thread.hpp",
    "Status initialize();\n",
    "Status initialize();\n"
    "Status set_pre_dispatch_hook(PreDispatchHook hook);\n",
)
replace_once(
    "kernel/task/thread.hpp",
    "Status request_yield();\nStatus sleep_current(uint64_t timer_ticks);\n",
    "Status request_yield();\n"
    "Status block_current();\n"
    "Status wake_user(ThreadId id, uint64_t accumulator);\n"
    "Status sleep_current(uint64_t timer_ticks);\n",
)

replace_once(
    "kernel/task/thread.cpp",
    "bool g_preemptive_timed_out = false;\n",
    "bool g_preemptive_timed_out = false;\n"
    "PreDispatchHook g_pre_dispatch_hook = nullptr;\n",
)
replace_once(
    "kernel/task/thread.cpp",
    "    static_cast<void>(activate_slot(kInvalidSlot));\n"
    "    return &g_timeout_return_frame;\n"
    "}\n"
    "#endif\n\n"
    "arch::x86_64::interrupts::InterruptFrame* software_interrupt_schedule(\n",
    "    static_cast<void>(activate_slot(kInvalidSlot));\n"
    "    return &g_timeout_return_frame;\n"
    "}\n\n"
    "arch::x86_64::interrupts::InterruptFrame* prepare_blocked_return(\n"
    "    size_t previous) {\n"
    "    Slot& old = g_slots[previous];\n"
    "    g_current = kInvalidSlot;\n"
    "    g_preemptive_active = false;\n"
    "    g_preemptive_timed_out = false;\n"
    "    g_timeout_return_frame = {};\n"
    "    uintptr_t top = reinterpret_cast<uintptr_t>(\n"
    "        g_timeout_return_stack + sizeof(g_timeout_return_stack));\n"
    "    top &= ~static_cast<uintptr_t>(0xFU);\n"
    "    const uintptr_t target_stack = top - sizeof(uint64_t);\n"
    "    *reinterpret_cast<uint64_t*>(target_stack) = 0U;\n"
    "    g_timeout_return_frame.rip = reinterpret_cast<uint64_t>(\n"
    "        &x86_64_thread_timeout_return);\n"
    "    g_timeout_return_frame.cs = arch::x86_64::gdt::KERNEL_CODE_SELECTOR;\n"
    "    g_timeout_return_frame.rflags = UINT64_C(0x2);\n"
    "    g_timeout_return_frame.rsp = target_stack;\n"
    "    g_timeout_return_frame.ss = arch::x86_64::gdt::KERNEL_DATA_SELECTOR;\n"
    "    static_cast<void>(old);\n"
    "    static_cast<void>(activate_slot(kInvalidSlot));\n"
    "    return &g_timeout_return_frame;\n"
    "}\n"
    "#endif\n\n"
    "arch::x86_64::interrupts::InterruptFrame* software_interrupt_schedule(\n",
)
replace_once(
    "kernel/task/thread.cpp",
    "#if defined(KUROGANE_HOST_TEST)\n"
    "        return &frame;\n"
    "#else\n"
    "        __asm__ volatile(\"sti; hlt; cli\" : : : \"memory\");\n"
    "#endif\n",
    "#if defined(KUROGANE_HOST_TEST)\n"
    "        return &frame;\n"
    "#else\n"
    "        if (old.state == State::Blocked) {\n"
    "            return prepare_blocked_return(previous);\n"
    "        }\n"
    "        __asm__ volatile(\"sti; hlt; cli\" : : : \"memory\");\n"
    "#endif\n",
)
replace_once(
    "kernel/task/thread.cpp",
    "} // namespace\n\nStatus initialize() {\n",
    "} // namespace\n\n"
    "Status set_pre_dispatch_hook(PreDispatchHook hook) {\n"
    "    if (hook == nullptr) return Status::InvalidArgument;\n"
    "    const uint64_t flags = save_and_disable_interrupts();\n"
    "    if (g_pre_dispatch_hook != nullptr && g_pre_dispatch_hook != hook) {\n"
    "        restore_interrupts(flags);\n"
    "        return Status::Busy;\n"
    "    }\n"
    "    g_pre_dispatch_hook = hook;\n"
    "    restore_interrupts(flags);\n"
    "    return Status::Ok;\n"
    "}\n\n"
    "Status initialize() {\n",
)
replace_once(
    "kernel/task/thread.cpp",
    "    if (!g_initialized) {\n"
    "        return Status::NotInitialized;\n"
    "    }\n"
    "    const uint64_t flags = save_and_disable_interrupts();\n"
    "    if (g_run_active || g_current != kInvalidSlot || g_preemptive_active) {\n",
    "    if (!g_initialized) {\n"
    "        return Status::NotInitialized;\n"
    "    }\n"
    "    if (g_pre_dispatch_hook != nullptr) {\n"
    "        g_pre_dispatch_hook();\n"
    "    }\n"
    "    const uint64_t flags = save_and_disable_interrupts();\n"
    "    if (g_run_active || g_current != kInvalidSlot || g_preemptive_active) {\n",
)
replace_once(
    "kernel/task/thread.cpp",
    "Status sleep_current(uint64_t timer_count) {\n",
    "Status block_current() {\n"
    "    if (!g_initialized) return Status::NotInitialized;\n"
    "    const uint64_t flags = save_and_disable_interrupts();\n"
    "    if (g_current == kInvalidSlot || !g_run_active) {\n"
    "        restore_interrupts(flags);\n"
    "        return Status::NotRunning;\n"
    "    }\n"
    "    Slot& slot = g_slots[g_current];\n"
    "    if (slot.process_id == 0U || slot.state != State::Running) {\n"
    "        restore_interrupts(flags);\n"
    "        return Status::CorruptContext;\n"
    "    }\n"
    "    slot.wake_tick = 0U;\n"
    "    slot.yield_requested = false;\n"
    "    slot.state = State::Blocked;\n"
    "    restore_interrupts(flags);\n"
    "    return Status::Ok;\n"
    "}\n\n"
    "Status wake_user(ThreadId id, uint64_t accumulator) {\n"
    "    if (!g_initialized) return Status::NotInitialized;\n"
    "    size_t index = 0U;\n"
    "    if (!decode_id(id, &index)) return Status::NotFound;\n"
    "    const uint64_t flags = save_and_disable_interrupts();\n"
    "    Slot& slot = g_slots[index];\n"
    "    if (slot.state != State::Blocked || slot.id != id ||\n"
    "        slot.process_id == 0U || slot.interrupt_frame == nullptr ||\n"
    "        (slot.interrupt_frame->cs & 3U) != 3U) {\n"
    "        restore_interrupts(flags);\n"
    "        return Status::NotFound;\n"
    "    }\n"
    "    slot.interrupt_frame->rax = accumulator;\n"
    "    slot.state = State::Ready;\n"
    "    slot.wake_tick = 0U;\n"
    "    slot.yield_requested = false;\n"
    "    restore_interrupts(flags);\n"
    "    return Status::Ok;\n"
    "}\n\n"
    "Status sleep_current(uint64_t timer_count) {\n",
)

# ---------------------------------------------------------------------------
# Runtime: bounded waiter records never retain userspace pointers. The network
# stack is progressed in kernel context before a preemptive userspace dispatch.
# ---------------------------------------------------------------------------
replace_once(
    "kernel/user/runtime.hpp",
    "    InterruptRegistrationFailed,\n    NotInitialized,\n",
    "    InterruptRegistrationFailed,\n"
    "    SchedulerRegistrationFailed,\n"
    "    NotInitialized,\n",
)
replace_once(
    "kernel/user/runtime_base.inc",
    "#include \"../net/service.hpp\"\n",
    "#include \"../net/service.hpp\"\n"
    "#include \"../net/socket.hpp\"\n",
)
replace_once(
    "kernel/user/runtime_base.inc",
    "uint64_t g_audio_owner_pid = 0U;\n\n"
    "uint64_t save_and_disable_interrupts() {\n",
    "uint64_t g_audio_owner_pid = 0U;\n\n"
    "struct SocketWaiter {\n"
    "    uint64_t pid;\n"
    "    threading::ThreadId tid;\n"
    "    net::socket::Handle socket;\n"
    "    uint32_t requested;\n"
    "    uint64_t deadline_tick;\n"
    "    bool finite_timeout;\n"
    "    bool active;\n"
    "};\n\n"
    "SocketWaiter g_socket_waiters[kMaximumContexts]{};\n\n"
    "ku_status_t socket_wait_status(net::socket::Status status) {\n"
    "    using SocketStatus = net::socket::Status;\n"
    "    switch (status) {\n"
    "        case SocketStatus::Ok:\n"
    "        case SocketStatus::AlreadyInitialized: return KU_STATUS_OK;\n"
    "        case SocketStatus::InvalidArgument:\n"
    "        case SocketStatus::StaleHandle: return KU_STATUS_INVALID_ARGUMENT;\n"
    "        case SocketStatus::NotSupported: return KU_STATUS_NOT_SUPPORTED;\n"
    "        case SocketStatus::CapacityReached: return KU_STATUS_OUT_OF_MEMORY;\n"
    "        case SocketStatus::AccessDenied: return KU_STATUS_ACCESS_DENIED;\n"
    "        case SocketStatus::AddressInUse: return KU_STATUS_ALREADY_EXISTS;\n"
    "        case SocketStatus::WouldBlock: return KU_STATUS_WOULD_BLOCK;\n"
    "        case SocketStatus::BufferTooSmall:\n"
    "        case SocketStatus::PayloadTooLarge: return KU_STATUS_OUT_OF_RANGE;\n"
    "        case SocketStatus::NotInitialized:\n"
    "        case SocketStatus::NotBound:\n"
    "        case SocketStatus::NotConnected: return KU_STATUS_BAD_STATE;\n"
    "        case SocketStatus::TransportError: return KU_STATUS_IO_ERROR;\n"
    "    }\n"
    "    return KU_STATUS_IO_ERROR;\n"
    "}\n\n"
    "void clear_socket_waiter(threading::ThreadId tid) {\n"
    "    for (SocketWaiter& waiter : g_socket_waiters) {\n"
    "        if (waiter.active && waiter.tid == tid) waiter = {};\n"
    "    }\n"
    "}\n\n"
    "SocketWaiter* reserve_socket_waiter(threading::ThreadId tid) {\n"
    "    clear_socket_waiter(tid);\n"
    "    for (SocketWaiter& waiter : g_socket_waiters) {\n"
    "        if (!waiter.active) return &waiter;\n"
    "    }\n"
    "    return nullptr;\n"
    "}\n\n"
    "void socket_wait_pre_dispatch() {\n"
    "    const uint64_t now = threading::timer_ticks();\n"
    "    for (SocketWaiter& waiter : g_socket_waiters) {\n"
    "        if (!waiter.active) continue;\n"
    "        threading::Stat stat{};\n"
    "        if (threading::stat(waiter.tid, &stat) != threading::Status::Ok ||\n"
    "            stat.process_id != waiter.pid || stat.state != threading::State::Blocked) {\n"
    "            waiter = {};\n"
    "            continue;\n"
    "        }\n\n"
    "        uint32_t ready = net::socket::ReadyNone;\n"
    "        const net::socket::Status status = net::socket::readiness(\n"
    "            waiter.pid, waiter.socket, waiter.requested, &ready);\n"
    "        uint64_t completion = 0U;\n"
    "        bool complete = false;\n"
    "        if (status != net::socket::Status::Ok) {\n"
    "            completion = static_cast<uint64_t>(socket_wait_status(status));\n"
    "            complete = true;\n"
    "        } else if ((ready & waiter.requested) != 0U) {\n"
    "            completion = ready;\n"
    "            complete = true;\n"
    "        } else if (waiter.finite_timeout && now >= waiter.deadline_tick) {\n"
    "            completion = static_cast<uint64_t>(KU_STATUS_TIMED_OUT);\n"
    "            complete = true;\n"
    "        }\n"
    "        if (!complete) continue;\n"
    "        const threading::ThreadId tid = waiter.tid;\n"
    "        waiter = {};\n"
    "        static_cast<void>(threading::wake_user(tid, completion));\n"
    "    }\n"
    "}\n\n"
    "uint64_t save_and_disable_interrupts() {\n",
)
replace_once(
    "kernel/user/runtime_base.inc",
    "        case KU_SYS_WAIT: {\n",
    "        case KU_SYS_SOCKET_WAIT: {\n"
    "            if (frame.rdi == KU_SOCKET_INVALID || frame.rsi == 0U ||\n"
    "                (frame.rsi & ~static_cast<uint64_t>(KU_SOCKET_READY_ALL)) != 0U) {\n"
    "                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);\n"
    "                return;\n"
    "            }\n"
    "            uint32_t ready = net::socket::ReadyNone;\n"
    "            const net::socket::Status readiness_status = net::socket::readiness(\n"
    "                context->pid, static_cast<net::socket::Handle>(frame.rdi),\n"
    "                static_cast<uint32_t>(frame.rsi), &ready);\n"
    "            if (readiness_status != net::socket::Status::Ok) {\n"
    "                frame.rax = static_cast<uint64_t>(socket_wait_status(readiness_status));\n"
    "                return;\n"
    "            }\n"
    "            if ((ready & static_cast<uint32_t>(frame.rsi)) != 0U) {\n"
    "                frame.rax = ready;\n"
    "                return;\n"
    "            }\n"
    "            if (frame.rdx == 0U) {\n"
    "                frame.rax = static_cast<uint64_t>(KU_STATUS_TIMED_OUT);\n"
    "                return;\n"
    "            }\n"
    "            SocketWaiter* waiter = reserve_socket_waiter(context->tid);\n"
    "            if (waiter == nullptr) {\n"
    "                frame.rax = static_cast<uint64_t>(KU_STATUS_OUT_OF_MEMORY);\n"
    "                return;\n"
    "            }\n"
    "            const uint64_t now = threading::timer_ticks();\n"
    "            const bool finite = frame.rdx != UINT64_MAX;\n"
    "            const uint64_t deadline = !finite || frame.rdx > UINT64_MAX - now\n"
    "                ? UINT64_MAX : now + frame.rdx;\n"
    "            *waiter = {\n"
    "                context->pid, context->tid,\n"
    "                static_cast<net::socket::Handle>(frame.rdi),\n"
    "                static_cast<uint32_t>(frame.rsi), deadline, finite, true};\n"
    "            if (threading::block_current() != threading::Status::Ok) {\n"
    "                *waiter = {};\n"
    "                frame.rax = static_cast<uint64_t>(KU_STATUS_BAD_STATE);\n"
    "                return;\n"
    "            }\n"
    "            // The saved Ring-3 frame cannot be selected while Blocked.\n"
    "            // wake_user() overwrites RAX with readiness or a terminal status.\n"
    "            frame.rax = static_cast<uint64_t>(KU_STATUS_WOULD_BLOCK);\n"
    "            return;\n"
    "        }\n"
    "        case KU_SYS_WAIT: {\n",
)
replace_once(
    "kernel/user/runtime_base.inc",
    "Status cleanup(Context& context) {\n    Status result = Status::Ok;\n",
    "Status cleanup(Context& context) {\n"
    "    Status result = Status::Ok;\n"
    "    clear_socket_waiter(context.tid);\n",
)
replace_once(
    "kernel/user/runtime_base.inc",
    "    for (size_t index = 0U; index < kMaximumContexts; ++index) {\n"
    "        g_context_storage[index] = {};\n"
    "        g_context_storage_used[index] = false;\n"
    "    }\n"
    "    if (!arch::x86_64::interrupts::register_handler(kSyscallVector, syscall_handler) ||\n",
    "    for (size_t index = 0U; index < kMaximumContexts; ++index) {\n"
    "        g_context_storage[index] = {};\n"
    "        g_context_storage_used[index] = false;\n"
    "        g_socket_waiters[index] = {};\n"
    "    }\n"
    "    if (threading::set_pre_dispatch_hook(socket_wait_pre_dispatch) !=\n"
    "        threading::Status::Ok) {\n"
    "        return Status::SchedulerRegistrationFailed;\n"
    "    }\n"
    "    if (!arch::x86_64::interrupts::register_handler(kSyscallVector, syscall_handler) ||\n",
)
replace_once(
    "kernel/user/runtime_base.inc",
    "        case Status::InterruptRegistrationFailed:\n"
    "            return \"cannot install ring-3 syscall gate\";\n"
    "        case Status::NotInitialized: return \"user runtime not initialized\";\n",
    "        case Status::InterruptRegistrationFailed:\n"
    "            return \"cannot install ring-3 syscall gate\";\n"
    "        case Status::SchedulerRegistrationFailed:\n"
    "            return \"cannot install ring-3 readiness scheduler hook\";\n"
    "        case Status::NotInitialized: return \"user runtime not initialized\";\n",
)

# ---------------------------------------------------------------------------
# Public ABI / SDK: append-only syscall #67. Ready bits are returned in RAX so
# the kernel never holds a userspace output pointer across a blocking wait.
# ---------------------------------------------------------------------------
replace_once(
    "sdk/include/kurogane/syscall.h",
    "    KU_SYS_DEVICE_RESOURCE = 66\n",
    "    KU_SYS_DEVICE_RESOURCE = 66,\n"
    "    KU_SYS_SOCKET_WAIT = 67\n",
)
replace_once(
    "sdk/include/kurogane/network.h",
    "/*\n"
    " * Scheduler-friendly readiness wait. Socket operations remain non-blocking;\n"
    " * callers that want to wait sleep between readiness probes instead of spinning.\n"
    " * timeout_ticks == 0 performs a single probe. UINT64_MAX waits indefinitely.\n"
    " */\n"
    "static inline ku_status_t ku_socket_wait(\n"
    "    ku_socket_t socket,\n"
    "    uint32_t requested,\n"
    "    uint64_t timeout_ticks,\n"
    "    uint32_t* ready) {\n"
    "    uint64_t elapsed = 0U;\n"
    "    for (;;) {\n"
    "        ku_status_t status = ku_socket_poll(socket, requested, ready);\n"
    "        if (status != KU_STATUS_OK) return status;\n"
    "        if ((*ready & requested) != 0U) return KU_STATUS_OK;\n"
    "        if (timeout_ticks == 0U || elapsed >= timeout_ticks) {\n"
    "            return KU_STATUS_TIMED_OUT;\n"
    "        }\n"
    "        status = ku_sleep(1U);\n"
    "        if (status != KU_STATUS_OK) return status;\n"
    "        if (elapsed != UINT64_MAX) ++elapsed;\n"
    "    }\n"
    "}\n",
    "/*\n"
    " * Event-driven readiness wait. A not-ready socket blocks the current Ring-3\n"
    " * thread in the scheduler; kernel-side network progress wakes that exact saved\n"
    " * syscall frame. timeout_ticks == 0 performs one probe. UINT64_MAX is infinite.\n"
    " */\n"
    "static inline ku_status_t ku_socket_wait(\n"
    "    ku_socket_t socket,\n"
    "    uint32_t requested,\n"
    "    uint64_t timeout_ticks,\n"
    "    uint32_t* ready) {\n"
    "    ku_result_t result;\n"
    "    if (socket == KU_SOCKET_INVALID || ready == NULL || requested == 0U ||\n"
    "        (requested & ~KU_SOCKET_READY_ALL) != 0U) {\n"
    "        return KU_STATUS_INVALID_ARGUMENT;\n"
    "    }\n"
    "    *ready = KU_SOCKET_READY_NONE;\n"
    "    result = ku_syscall3(KU_SYS_SOCKET_WAIT, socket, requested, timeout_ticks);\n"
    "    if (result < 0) return (ku_status_t)result;\n"
    "    if (result == 0 || ((uint64_t)result & ~KU_SOCKET_READY_ALL) != 0U ||\n"
    "        (((uint32_t)result & requested) == 0U)) {\n"
    "        return KU_STATUS_CORRUPT_DATA;\n"
    "    }\n"
    "    *ready = (uint32_t)result;\n"
    "    return KU_STATUS_OK;\n"
    "}\n",
)

# ABI regression coverage for every post-3.4 syscall number.
replace_once(
    "tests/test_sdk_abi.cpp",
    "    static_assert(KU_SYS_EVENT_CLOSE == 54);\n\n",
    "    static_assert(KU_SYS_EVENT_CLOSE == 54);\n"
    "    static_assert(KU_SYS_IPC_QUERY == 55);\n"
    "    static_assert(KU_SYS_DNS_RESOLVE_A == 56);\n"
    "    static_assert(KU_SYS_SOCKET_CREATE == 57);\n"
    "    static_assert(KU_SYS_SOCKET_BIND == 58);\n"
    "    static_assert(KU_SYS_SOCKET_CONNECT == 59);\n"
    "    static_assert(KU_SYS_SOCKET_SEND == 60);\n"
    "    static_assert(KU_SYS_SOCKET_RECEIVE == 61);\n"
    "    static_assert(KU_SYS_SOCKET_CLOSE == 62);\n"
    "    static_assert(KU_SYS_SOCKET_POLL == 63);\n"
    "    static_assert(KU_SYS_DEVICE_ENUMERATE == 64);\n"
    "    static_assert(KU_SYS_DEVICE_QUERY == 65);\n"
    "    static_assert(KU_SYS_DEVICE_RESOURCE == 66);\n"
    "    static_assert(KU_SYS_SOCKET_WAIT == 67);\n\n",
)

# ---------------------------------------------------------------------------
# Host socket readiness: fake TCP progression proves WRITE/CONNECTED, HANGUP
# and ERROR flags use the same generic readiness path as runtime waiters.
# ---------------------------------------------------------------------------
replace_once(
    "tests/test_socket.cpp",
    "    size_t sent_size = 0U;\n};\n",
    "    size_t sent_size = 0U;\n"
    "    bool tcp_establish = false;\n"
    "    bool tcp_peer_close = false;\n"
    "    bool tcp_error = false;\n"
    "};\n",
)
replace_once(
    "tests/test_socket.cpp",
    "net::Status unsupported_tcp_begin_connect(\n"
    "    void* opaque,\n"
    "    net::tcp_client::Client* client,\n"
    "    const net::IPv4Address& destination,\n"
    "    uint16_t source_port,\n"
    "    uint16_t destination_port,\n"
    "    uint32_t initial_sequence) {\n"
    "    (void)opaque;\n"
    "    (void)client;\n"
    "    (void)destination;\n"
    "    (void)source_port;\n"
    "    (void)destination_port;\n"
    "    (void)initial_sequence;\n"
    "    return net::Status::UnsupportedProtocol;\n"
    "}\n\n"
    "net::Status unsupported_tcp_progress(void* opaque, net::tcp_client::Client* client) {\n"
    "    (void)opaque;\n"
    "    (void)client;\n"
    "    return net::Status::UnsupportedProtocol;\n"
    "}\n",
    "net::Status fake_tcp_begin_connect(\n"
    "    void* opaque,\n"
    "    net::tcp_client::Client* client,\n"
    "    const net::IPv4Address& destination,\n"
    "    uint16_t source_port,\n"
    "    uint16_t destination_port,\n"
    "    uint32_t initial_sequence) {\n"
    "    auto* transport = static_cast<FakeTransport*>(opaque);\n"
    "    if (transport == nullptr || client == nullptr || source_port == 0U ||\n"
    "        destination_port == 0U || initial_sequence == 0U) {\n"
    "        return net::Status::InvalidArgument;\n"
    "    }\n"
    "    client->peer = destination;\n"
    "    client->local_port = source_port;\n"
    "    client->remote_port = destination_port;\n"
    "    client->state = net::tcp_client::State::SynSent;\n"
    "    client->connected = false;\n"
    "    return net::Status::WouldBlock;\n"
    "}\n\n"
    "net::Status fake_tcp_progress(void* opaque, net::tcp_client::Client* client) {\n"
    "    auto* transport = static_cast<FakeTransport*>(opaque);\n"
    "    if (transport == nullptr || client == nullptr) return net::Status::InvalidArgument;\n"
    "    if (transport->tcp_error) {\n"
    "        client->state = net::tcp_client::State::Error;\n"
    "        client->connected = false;\n"
    "        return net::Status::InterfaceError;\n"
    "    }\n"
    "    if (transport->tcp_peer_close) {\n"
    "        client->state = net::tcp_client::State::CloseWait;\n"
    "        client->connected = true;\n"
    "        client->peer_closed = true;\n"
    "        return net::Status::Ok;\n"
    "    }\n"
    "    if (transport->tcp_establish) {\n"
    "        client->state = net::tcp_client::State::Established;\n"
    "        client->connected = true;\n"
    "        return net::Status::Ok;\n"
    "    }\n"
    "    return net::Status::WouldBlock;\n"
    "}\n",
)
replace_once(
    "tests/test_socket.cpp",
    "net::Status unsupported_tcp_try_send(\n",
    "net::Status fake_tcp_try_send(\n",
)
replace_once(
    "tests/test_socket.cpp",
    "    (void)opaque;\n"
    "    (void)client;\n"
    "    (void)data;\n"
    "    (void)length;\n"
    "    if (out_sent != nullptr) *out_sent = 0U;\n"
    "    return net::Status::UnsupportedProtocol;\n"
    "}\n\n"
    "net::Status unsupported_tcp_try_receive(\n",
    "    auto* transport = static_cast<FakeTransport*>(opaque);\n"
    "    if (out_sent != nullptr) *out_sent = 0U;\n"
    "    if (transport == nullptr || client == nullptr || data == nullptr ||\n"
    "        length == 0U || out_sent == nullptr) return net::Status::InvalidArgument;\n"
    "    if (!client->connected) return net::Status::WouldBlock;\n"
    "    *out_sent = length;\n"
    "    return net::Status::Ok;\n"
    "}\n\n"
    "net::Status fake_tcp_try_receive(\n",
)
replace_once(
    "tests/test_socket.cpp",
    "    (void)opaque;\n"
    "    (void)client;\n"
    "    (void)output;\n"
    "    (void)output_capacity;\n"
    "    if (out_length != nullptr) *out_length = 0U;\n"
    "    return net::Status::UnsupportedProtocol;\n"
    "}\n\n"
    "net::Status unsupported_tcp_begin_close(void* opaque, net::tcp_client::Client* client) {\n"
    "    (void)opaque;\n"
    "    (void)client;\n"
    "    return net::Status::UnsupportedProtocol;\n"
    "}\n",
    "    auto* transport = static_cast<FakeTransport*>(opaque);\n"
    "    if (out_length != nullptr) *out_length = 0U;\n"
    "    if (transport == nullptr || client == nullptr || output == nullptr ||\n"
    "        output_capacity == 0U || out_length == nullptr) {\n"
    "        return net::Status::InvalidArgument;\n"
    "    }\n"
    "    return client->peer_closed ? net::Status::Ok : net::Status::WouldBlock;\n"
    "}\n\n"
    "net::Status fake_tcp_begin_close(void* opaque, net::tcp_client::Client* client) {\n"
    "    if (opaque == nullptr || client == nullptr) return net::Status::InvalidArgument;\n"
    "    client->state = net::tcp_client::State::Closed;\n"
    "    client->connected = false;\n"
    "    return net::Status::Ok;\n"
    "}\n",
)
replace_once(
    "tests/test_socket.cpp",
    "        unsupported_tcp_begin_connect,\n"
    "        unsupported_tcp_progress,\n"
    "        unsupported_tcp_try_send,\n"
    "        unsupported_tcp_try_receive,\n"
    "        unsupported_tcp_begin_close,\n",
    "        fake_tcp_begin_connect,\n"
    "        fake_tcp_progress,\n"
    "        fake_tcp_try_send,\n"
    "        fake_tcp_try_receive,\n"
    "        fake_tcp_begin_close,\n",
)
replace_once(
    "tests/test_socket.cpp",
    "    CHECK(active_count(owner) == 2U);\n"
    "    release_process(owner);\n",
    "    Handle tcp = INVALID_HANDLE;\n"
    "    CHECK(create(owner, Type::Stream, Protocol::Tcp, &tcp) == Status::Ok);\n"
    "    CHECK(connect(owner, tcp, remote) == Status::WouldBlock);\n"
    "    uint32_t tcp_ready = ReadyNone;\n"
    "    CHECK(readiness(owner, tcp, ReadyWrite | ReadyConnected, &tcp_ready) == Status::Ok);\n"
    "    CHECK(tcp_ready == ReadyNone);\n"
    "    transport.tcp_establish = true;\n"
    "    CHECK(readiness(owner, tcp, ReadyWrite | ReadyConnected, &tcp_ready) == Status::Ok);\n"
    "    CHECK((tcp_ready & (ReadyWrite | ReadyConnected)) ==\n"
    "        (ReadyWrite | ReadyConnected));\n"
    "    transport.tcp_peer_close = true;\n"
    "    CHECK(readiness(owner, tcp, ReadyRead | ReadyHangup, &tcp_ready) == Status::Ok);\n"
    "    CHECK((tcp_ready & (ReadyRead | ReadyHangup)) == (ReadyRead | ReadyHangup));\n"
    "    transport.tcp_peer_close = false;\n"
    "    transport.tcp_error = true;\n"
    "    CHECK(readiness(owner, tcp, ReadyError, &tcp_ready) == Status::Ok);\n"
    "    CHECK((tcp_ready & ReadyError) != 0U);\n"
    "    transport.tcp_error = false;\n\n"
    "    CHECK(active_count(owner) == 3U);\n"
    "    release_process(owner);\n",
)

# ---------------------------------------------------------------------------
# Real Ring-3 proof: parent waits while a child sleeps, then sends loopback UDP.
# A second empty wait proves timeout wakes a Blocked frame without polling SDK.
# ---------------------------------------------------------------------------
worker = Path("userspace/system/socket-wake-worker/main.c")
worker.parent.mkdir(parents=True, exist_ok=True)
require_absent(str(worker))
worker.write_text(
    '#include "../../runtime/user.h"\n\n'
    '#include <kurogane/network.h>\n\n'
    '#define SOCKET_WAKE_PORT UINT16_C(45352)\n\n'
    'static ku_ipv4_endpoint wake_endpoint(void) {\n'
    '    ku_ipv4_endpoint endpoint = {{127U, 0U, 0U, 1U}, SOCKET_WAKE_PORT, 0U};\n'
    '    return endpoint;\n'
    '}\n\n'
    '__attribute__((noreturn)) void _start(void) {\n'
    "    static const uint8_t payload[] = {'w', 'a', 'k', 'e'};\n"
    '    ku_result_t result;\n'
    '    ku_socket_t socket;\n'
    '    const ku_ipv4_endpoint endpoint = wake_endpoint();\n'
    '    if (ku_sleep(3U) != KU_STATUS_OK) ku_exit(1);\n'
    '    result = ku_socket_create(KU_SOCKET_DATAGRAM, KU_SOCKET_PROTOCOL_UDP);\n'
    '    if (result <= 0) ku_exit(2);\n'
    '    socket = (ku_socket_t)result;\n'
    '    if (ku_socket_connect(socket, &endpoint) != KU_STATUS_OK) ku_exit(3);\n'
    '    if (ku_socket_send(socket, payload, sizeof(payload)) != (ku_result_t)sizeof(payload)) {\n'
    '        ku_exit(4);\n'
    '    }\n'
    '    if (ku_socket_close(socket) != KU_STATUS_OK) ku_exit(5);\n'
    '    ku_exit(0);\n'
    '}\n'
)

replace_once(
    "userspace/system/socket-probe/main.c",
    "#define SOCKET_EXIT_PORT UINT16_C(45351)\n",
    "#define SOCKET_EXIT_PORT UINT16_C(45351)\n"
    "#define SOCKET_WAKE_PORT UINT16_C(45352)\n",
)
replace_once(
    "userspace/system/socket-probe/main.c",
    "static int qualify_handle_generation(void) {\n",
    "static int qualify_blocking_readiness(void) {\n"
    "    static const uint8_t payload[] = {'w', 'a', 'k', 'e'};\n"
    "    uint8_t received[sizeof(payload)];\n"
    "    ku_ipv4_endpoint source = {{0U, 0U, 0U, 0U}, 0U, 0U};\n"
    "    uint32_t ready = KU_SOCKET_READY_NONE;\n"
    "    int32_t worker_exit = -1;\n"
    "    uint32_t attempts;\n"
    "    ku_result_t result;\n"
    "    ku_result_t worker;\n"
    "    ku_socket_t receiver;\n"
    "    const ku_ipv4_endpoint endpoint = loopback_endpoint(SOCKET_WAKE_PORT);\n\n"
    "    result = ku_socket_create(KU_SOCKET_DATAGRAM, KU_SOCKET_PROTOCOL_UDP);\n"
    "    if (result <= 0) return 0;\n"
    "    receiver = (ku_socket_t)result;\n"
    "    if (ku_socket_bind(receiver, &endpoint) != KU_STATUS_OK) return 0;\n"
    "    if (ku_socket_poll(receiver, KU_SOCKET_READY_READ, &ready) != KU_STATUS_OK ||\n"
    "        ready != KU_SOCKET_READY_NONE) return 0;\n\n"
    "    worker = u_spawn(\"/system/sockwake\");\n"
    "    if (worker <= 0) return 0;\n"
    "    ready = KU_SOCKET_READY_NONE;\n"
    "    if (ku_socket_wait(receiver, KU_SOCKET_READY_READ, UINT64_C(64), &ready) !=\n"
    "            KU_STATUS_OK || (ready & KU_SOCKET_READY_READ) == 0U) {\n"
    "        return 0;\n"
    "    }\n"
    "    result = ku_socket_receive(receiver, received, sizeof(received), &source);\n"
    "    if (result != (ku_result_t)sizeof(payload) ||\n"
    "        !bytes_equal(received, payload, sizeof(payload)) || !is_loopback(&source)) {\n"
    "        return 0;\n"
    "    }\n"
    "    for (attempts = 0U; attempts < 64U; ++attempts) {\n"
    "        const ku_status_t status = ku_wait((uint64_t)worker, &worker_exit);\n"
    "        if (status == KU_STATUS_OK) break;\n"
    "        if (status != KU_STATUS_WOULD_BLOCK) return 0;\n"
    "        (void)ku_yield();\n"
    "    }\n"
    "    if (worker_exit != 0) return 0;\n\n"
    "    ready = KU_SOCKET_READY_NONE;\n"
    "    if (ku_socket_wait(receiver, KU_SOCKET_READY_READ, UINT64_C(2), &ready) !=\n"
    "            KU_STATUS_TIMED_OUT || ready != KU_SOCKET_READY_NONE) {\n"
    "        return 0;\n"
    "    }\n"
    "    return ku_socket_close(receiver) == KU_STATUS_OK;\n"
    "}\n\n"
    "static int qualify_handle_generation(void) {\n",
)
replace_once(
    "userspace/system/socket-probe/main.c",
    "    if (!qualify_handle_generation()) {\n",
    "    if (!qualify_blocking_readiness()) {\n"
    "        (void)u_puts(\"[TEST] socket_readiness: FAIL\\n\");\n"
    "        ku_exit(4);\n"
    "    }\n"
    "    (void)u_puts(\"[TEST] socket_readiness: PASS\\n\");\n\n"
    "    if (!qualify_handle_generation()) {\n",
)

# The existing qualification workflow keeps probes out of normal release PID1.
replace_once(
    ".github/workflows/qualify-socket-core.yml",
    "      - 'kernel/net/tcp_client.hpp'\n"
    "      - 'kernel/user/runtime.cpp'\n"
    "      - 'sdk/include/kurogane/network.h'\n",
    "      - 'kernel/net/tcp_client.hpp'\n"
    "      - 'kernel/task/thread.cpp'\n"
    "      - 'kernel/task/thread.hpp'\n"
    "      - 'kernel/user/runtime.cpp'\n"
    "      - 'kernel/user/runtime.hpp'\n"
    "      - 'kernel/user/runtime_base.inc'\n"
    "      - 'sdk/include/kurogane/network.h'\n"
    "      - 'sdk/include/kurogane/syscall.h'\n",
)
replace_once(
    ".github/workflows/qualify-socket-core.yml",
    "      - 'userspace/system/socket-exit-worker/**'\n",
    "      - 'userspace/system/socket-exit-worker/**'\n"
    "      - 'userspace/system/socket-wake-worker/**'\n",
)
replace_once(
    ".github/workflows/qualify-socket-core.yml",
    "      - 'tests/test_socket.cpp'\n",
    "      - 'tests/test_socket.cpp'\n"
    "      - 'tests/test_thread.cpp'\n"
    "      - 'tests/test_sdk_abi.cpp'\n",
)
replace_once(
    ".github/workflows/qualify-socket-core.yml",
    "              '    \"socketexit|userspace/system/socket-exit-worker/main.c|system/sockexit|c\"\\n'\n"
    "              '    \"socketprobe|userspace/system/socket-probe/main.c|system/sockprb|c\"\\n'\n",
    "              '    \"socketexit|userspace/system/socket-exit-worker/main.c|system/sockexit|c\"\\n'\n"
    "              '    \"socketwake|userspace/system/socket-wake-worker/main.c|system/sockwake|c\"\\n'\n"
    "              '    \"socketprobe|userspace/system/socket-probe/main.c|system/sockprb|c\"\\n'\n",
)
replace_once(
    ".github/workflows/qualify-socket-core.yml",
    "          grep -F 'system/sockexit' scripts/build-linux.sh\n"
    "          grep -F 'system/sockprb' scripts/build-linux.sh\n",
    "          grep -F 'system/sockexit' scripts/build-linux.sh\n"
    "          grep -F 'system/sockwake' scripts/build-linux.sh\n"
    "          grep -F 'system/sockprb' scripts/build-linux.sh\n",
)
replace_once(
    ".github/workflows/qualify-socket-core.yml",
    "            --require-marker '[TEST] socket_udp_roundtrip: PASS' \\\n"
    "            --require-marker '[TEST] socket_handle_generation: PASS' \\\n",
    "            --require-marker '[TEST] socket_udp_roundtrip: PASS' \\\n"
    "            --require-marker '[TEST] socket_readiness: PASS' \\\n"
    "            --require-marker '[TEST] socket_handle_generation: PASS' \\\n",
)

print("socket readiness source patch applied")
