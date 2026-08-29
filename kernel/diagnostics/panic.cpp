#include "panic.hpp"

#include "../arch/x86_64/apic.hpp"
#include "../drivers/framebuffer.hpp"
#include "../drivers/serial.hpp"
#include "../memory/kernel_virtual_memory.hpp"
#include "../memory/physical_memory.hpp"
#include "../task/process.hpp"
#include "../task/thread.hpp"
#include "../../common/version.h"

#include <stddef.h>
#include <stdint.h>

#ifndef KUROGANE_BUILD_ID
#define KUROGANE_BUILD_ID "build-id-unavailable"
#endif

#ifndef KUROGANE_COMMIT_ID
#define KUROGANE_COMMIT_ID "commit-id-unavailable"
#endif

extern "C" unsigned char kernel_stack_bottom[];
extern "C" unsigned char kernel_stack_top[];

namespace diagnostics::panic {
namespace {

constexpr graphics::Color kBackground = graphics::rgb(11U, 13U, 16U);
constexpr graphics::Color kPanel = graphics::rgb(20U, 22U, 27U);
constexpr graphics::Color kRed = graphics::rgb(185U, 28U, 28U);
constexpr graphics::Color kRedSoft = graphics::rgb(248U, 113U, 113U);
constexpr graphics::Color kText = graphics::rgb(229U, 231U, 235U);
constexpr graphics::Color kMuted = graphics::rgb(156U, 163U, 175U);
constexpr uint16_t kEventPanicTransition = UINT16_C(0xF001);
constexpr uint16_t kEventFaultInjection = UINT16_C(0xF002);

alignas(64) FatalSnapshot g_snapshot{};
uint64_t g_panic_sequence = 0U;
uint32_t g_panic_depth = 0U;
PanicSafeDumpWriter g_dump_writer = nullptr;
#if defined(KUROGANE_PANIC_TEST) && KUROGANE_PANIC_TEST
bool g_force_nested_fallback = false;
#endif

void copy_string(char* destination, size_t capacity, const char* source) {
    if (destination == nullptr || capacity == 0U) {
        return;
    }
    if (source == nullptr) {
        source = "(null)";
    }
    size_t index = 0U;
    while (index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
    for (++index; index < capacity; ++index) {
        destination[index] = '\0';
    }
}

uint16_t read_ss() {
    uint16_t value = 0U;
#if defined(__x86_64__)
    __asm__ volatile("mov %%ss, %0" : "=r"(value));
#endif
    return value;
}

uint32_t current_cpu() {
    return arch::x86_64::apic::prepared()
        ? arch::x86_64::apic::local_apic_id()
        : events::UNKNOWN_CPU;
}

Context context_from_frame(
    const arch::x86_64::interrupts::InterruptFrame& frame) {
    const uint64_t cpl = frame.cs & UINT64_C(3);
    if (cpl == UINT64_C(3)) {
        return Context::Userspace;
    }
    if (cpl == UINT64_C(0)) {
        return Context::Kernel;
    }
    return Context::Unknown;
}

uint64_t interrupted_rsp(
    arch::x86_64::interrupts::InterruptFrame& frame,
    Context context) {
    if (context == Context::Userspace) {
        return frame.rsp;
    }
    // Same-CPL x86-64 exceptions do not push SS:RSP. At C++ entry the first
    // byte after the saved RFLAGS is therefore the interrupted kernel RSP.
    // Do not read frame.rsp in this case: that memory is not part of the
    // hardware frame and would be a fabricated register value.
    return static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(&frame) +
        offsetof(arch::x86_64::interrupts::InterruptFrame, rsp));
}

void capture_identity(FatalSnapshot& snapshot) {
    snapshot.tid = threading::current();
    snapshot.pid = threading::current_process();
    copy_string(snapshot.thread_name, NAME_CAPACITY, "boot/kernel");
    copy_string(snapshot.process_name, NAME_CAPACITY, "kernel");

    if (snapshot.tid != threading::INVALID_THREAD_ID) {
        threading::Stat thread_stat{};
        if (threading::stat(snapshot.tid, &thread_stat) ==
            threading::Status::Ok) {
            copy_string(
                snapshot.thread_name,
                NAME_CAPACITY,
                thread_stat.name);
            if (snapshot.pid == 0U) {
                snapshot.pid = thread_stat.process_id;
            }
        }
    }

    if (snapshot.pid != process::INVALID_PROCESS_ID) {
        process::Stat process_stat{};
        if (process::stat(snapshot.pid, &process_stat) == process::Status::Ok) {
            copy_string(
                snapshot.process_name,
                NAME_CAPACITY,
                process_stat.name);
        }
    }
}

void capture_trace(
    FatalSnapshot& snapshot,
    const arch::x86_64::interrupts::InterruptFrame& frame) {
    snapshot.trace_count = 0U;
    if (frame.rip != 0U) {
        snapshot.trace[snapshot.trace_count++] = frame.rip;
    }
    if (snapshot.context != Context::Kernel || frame.rbp == 0U ||
        snapshot.trace_count >= TRACE_CAPACITY) {
        return;
    }

    uintptr_t stack_begin = reinterpret_cast<uintptr_t>(kernel_stack_bottom);
    uintptr_t stack_end = reinterpret_cast<uintptr_t>(kernel_stack_top);
    if (snapshot.tid != threading::INVALID_THREAD_ID) {
        threading::Stat thread_stat{};
        if (threading::stat(snapshot.tid, &thread_stat) ==
            threading::Status::Ok &&
            thread_stat.stack_bottom < thread_stat.stack_top) {
            stack_begin = thread_stat.stack_bottom;
            stack_end = thread_stat.stack_top;
        }
    }

    uintptr_t frame_pointer = static_cast<uintptr_t>(frame.rbp);
    while (snapshot.trace_count < TRACE_CAPACITY) {
        if (stack_end < 2U * sizeof(uint64_t) ||
            frame_pointer < stack_begin ||
            frame_pointer > stack_end - 2U * sizeof(uint64_t) ||
            (frame_pointer & (alignof(uint64_t) - 1U)) != 0U) {
            break;
        }
        const auto* chain = reinterpret_cast<const uint64_t*>(frame_pointer);
        const uintptr_t previous = static_cast<uintptr_t>(chain[0]);
        const uint64_t return_address = chain[1];
        if (return_address == 0U) {
            break;
        }
        snapshot.trace[snapshot.trace_count++] = return_address;
        if (previous <= frame_pointer) {
            break;
        }
        frame_pointer = previous;
    }
}

void capture_memory(FatalSnapshot& snapshot) {
    snapshot.pmm_available = memory::physical_memory_initialized();
    if (snapshot.pmm_available) {
        snapshot.pmm_total_frames = static_cast<uint64_t>(memory::total_frames());
        snapshot.pmm_used_frames = static_cast<uint64_t>(memory::used_frames());
        snapshot.pmm_free_frames = static_cast<uint64_t>(memory::free_frames());
    }
    snapshot.vmm_available = memory::kernel_virtual_memory::initialized();
    if (snapshot.vmm_available) {
        snapshot.vmm_root =
            memory::kernel_virtual_memory::active_root_table_physical();
    }
}

void capture_snapshot(
    FatalSnapshot& snapshot,
    arch::x86_64::interrupts::InterruptFrame& frame) {
    snapshot = {};
    snapshot.panic_sequence = __atomic_fetch_add(
        &g_panic_sequence, UINT64_C(1), __ATOMIC_RELAXED);
    snapshot.monotonic_tick = threading::timer_ticks();
    snapshot.cpu = current_cpu();
    snapshot.vector = static_cast<uint8_t>(frame.vector & UINT64_C(0xFF));
    snapshot.context = context_from_frame(frame);
    snapshot.cr2_valid = snapshot.vector == 14U;
    snapshot.cr2 = snapshot.cr2_valid
        ? arch::x86_64::interrupts::last_page_fault_address()
        : 0U;
    snapshot.wall_time_trustworthy = false;
    snapshot.dump_status = DumpStatus::UnavailableNoSafeWriter;
    snapshot.registers = frame;
    snapshot.registers.rsp = interrupted_rsp(frame, snapshot.context);
    snapshot.registers.ss = snapshot.context == Context::Userspace
        ? frame.ss
        : static_cast<uint64_t>(read_ss());
    copy_string(snapshot.reason, IDENTITY_CAPACITY, exception_name(snapshot.vector));
    copy_string(snapshot.subsystem, NAME_CAPACITY, "CPU/EXCEPTION");
    copy_string(snapshot.version, NAME_CAPACITY, KUROGANE_VERSION_STRING);
    copy_string(snapshot.build_id, IDENTITY_CAPACITY, KUROGANE_BUILD_ID);
    copy_string(snapshot.commit_id, IDENTITY_CAPACITY, KUROGANE_COMMIT_ID);
    capture_identity(snapshot);
    capture_memory(snapshot);
    capture_trace(snapshot, frame);
    snapshot.event_count = events::copy_recent(
        snapshot.events, SNAPSHOT_EVENT_CAPACITY);
}

void serial_u64(uint64_t value) {
    char digits[21];
    size_t count = 0U;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U) {
        serial::put(digits[--count]);
    }
}

void serial_key(const char* key, const char* value) {
    serial::write(key);
    serial::write("=");
    serial::write(value != nullptr ? value : "(null)");
    serial::put('\n');
}

void serial_key_u64(const char* key, uint64_t value) {
    serial::write(key);
    serial::write("=");
    serial_u64(value);
    serial::put('\n');
}

void serial_key_hex(const char* key, uint64_t value) {
    serial::write(key);
    serial::write("=");
    serial::write_hex(value);
    serial::put('\n');
}

const char* severity_name(events::Severity severity) {
    switch (severity) {
        case events::Severity::Trace: return "TRACE";
        case events::Severity::Debug: return "DEBUG";
        case events::Severity::Info: return "INFO";
        case events::Severity::Warn: return "WARN";
        case events::Severity::Error: return "ERROR";
        case events::Severity::Fatal: return "FATAL";
    }
    return "UNKNOWN";
}

void mirror_serial(const FatalSnapshot& snapshot) {
    if (!serial::ready()) {
        static_cast<void>(serial::init());
    }
    serial::write("\n=== KUROGANE_FATAL_BEGIN ===\n");
    serial_key("VERSION", snapshot.version);
    serial_key("BUILD_ID", snapshot.build_id);
    serial_key("COMMIT_ID", snapshot.commit_id);
    serial_key("SUBSYSTEM", snapshot.subsystem);
    serial_key("REASON", snapshot.reason);
    serial_key_u64("PANIC_SEQUENCE", snapshot.panic_sequence);
    serial_key_u64("VECTOR", snapshot.vector);
    serial_key_hex("ERROR_CODE", snapshot.registers.error_code);
    serial_key("CONTEXT", context_name(snapshot.context));
    if (snapshot.cpu == events::UNKNOWN_CPU) {
        serial_key("CPU_APIC", "UNKNOWN");
    } else {
        serial_key_u64("CPU_APIC", snapshot.cpu);
    }
    serial_key_u64("PID", snapshot.pid);
    serial_key_u64("TID", snapshot.tid);
    serial_key("PROCESS", snapshot.process_name);
    serial_key("THREAD", snapshot.thread_name);
    serial_key_u64("UPTIME_TICKS", snapshot.monotonic_tick);
    serial_key("WALL_TIME", snapshot.wall_time_trustworthy
        ? "TRUSTWORTHY"
        : "UNAVAILABLE: no panic-safe trustworthy wall clock snapshot");

    serial_key_hex("RIP", snapshot.registers.rip);
    serial_key_hex("RSP", snapshot.registers.rsp);
    serial_key_hex("RFLAGS", snapshot.registers.rflags);
    serial_key_hex("CS", snapshot.registers.cs);
    serial_key_hex("SS", snapshot.registers.ss);
    serial_key_hex("RAX", snapshot.registers.rax);
    serial_key_hex("RBX", snapshot.registers.rbx);
    serial_key_hex("RCX", snapshot.registers.rcx);
    serial_key_hex("RDX", snapshot.registers.rdx);
    serial_key_hex("RSI", snapshot.registers.rsi);
    serial_key_hex("RDI", snapshot.registers.rdi);
    serial_key_hex("RBP", snapshot.registers.rbp);
    serial_key_hex("R8", snapshot.registers.r8);
    serial_key_hex("R9", snapshot.registers.r9);
    serial_key_hex("R10", snapshot.registers.r10);
    serial_key_hex("R11", snapshot.registers.r11);
    serial_key_hex("R12", snapshot.registers.r12);
    serial_key_hex("R13", snapshot.registers.r13);
    serial_key_hex("R14", snapshot.registers.r14);
    serial_key_hex("R15", snapshot.registers.r15);
    if (snapshot.cr2_valid) {
        serial_key_hex("CR2", snapshot.cr2);
    } else {
        serial_key("CR2", "N/A");
    }

    if (snapshot.pmm_available) {
        serial_key_u64("PMM_TOTAL_FRAMES", snapshot.pmm_total_frames);
        serial_key_u64("PMM_USED_FRAMES", snapshot.pmm_used_frames);
        serial_key_u64("PMM_FREE_FRAMES", snapshot.pmm_free_frames);
    } else {
        serial_key("PMM", "UNAVAILABLE");
    }
    if (snapshot.vmm_available) {
        serial_key_hex("VMM_CR3", snapshot.vmm_root);
    } else {
        serial_key("VMM", "UNAVAILABLE");
    }

    serial::write("TRACE_COUNT=");
    serial_u64(static_cast<uint64_t>(snapshot.trace_count));
    serial::put('\n');
    for (size_t index = 0U; index < snapshot.trace_count; ++index) {
        serial::write("TRACE[");
        serial_u64(static_cast<uint64_t>(index));
        serial::write("]=");
        serial::write_hex(snapshot.trace[index]);
        serial::put('\n');
    }
    serial_key("SYMBOLS", "UNAVAILABLE: no panic-safe symbol resolver");

    serial::write("LAST_KERNEL_EVENTS_COUNT=");
    serial_u64(static_cast<uint64_t>(snapshot.event_count));
    serial::put('\n');
    for (size_t index = 0U; index < snapshot.event_count; ++index) {
        const events::Event& event = snapshot.events[index];
        serial::write("EVENT seq=");
        serial_u64(event.sequence);
        serial::write(" tick=");
        serial_u64(event.monotonic_tick);
        serial::write(" sev=");
        serial::write(severity_name(event.severity));
        serial::write(" cpu=");
        if (event.cpu == events::UNKNOWN_CPU) {
            serial::write("UNKNOWN");
        } else {
            serial_u64(event.cpu);
        }
        serial::write(" pid=");
        serial_u64(event.pid);
        serial::write(" tid=");
        serial_u64(event.tid);
        serial::write(" code=");
        serial::write_hex(event.code);
        serial::write(" subsystem=");
        serial::write(event.subsystem);
        serial::write(" message=");
        serial::write(event.message);
        serial::put('\n');
    }
    serial_key("DUMP", dump_status_message(snapshot.dump_status));
    serial::write("=== KUROGANE_FATAL_END ===\n");
}

void append_char(char* output, size_t capacity, size_t& length, char value) {
    if (length + 1U >= capacity) {
        return;
    }
    output[length++] = value;
    output[length] = '\0';
}

void append_text(char* output, size_t capacity, size_t& length, const char* text) {
    if (text == nullptr) {
        return;
    }
    while (*text != '\0' && length + 1U < capacity) {
        output[length++] = *text++;
    }
    output[length] = '\0';
}

void append_u64(char* output, size_t capacity, size_t& length, uint64_t value) {
    char digits[21];
    size_t count = 0U;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U) {
        append_char(output, capacity, length, digits[--count]);
    }
}

void append_hex(char* output, size_t capacity, size_t& length, uint64_t value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    append_text(output, capacity, length, "0x");
    bool significant = false;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const uint8_t digit = static_cast<uint8_t>((value >> shift) & 0xFU);
        if (digit != 0U || significant || shift == 0) {
            significant = true;
            append_char(output, capacity, length, digits[digit]);
        }
    }
}

bool draw_line(int32_t& y, graphics::Color color, const char* text) {
    if (y < 0 || static_cast<uint32_t>(y + 9) >= graphics::height()) {
        return false;
    }
    graphics::draw_text(16, y, text, color, kBackground, 1U, false);
    y += 10;
    return true;
}

void draw_pair_hex(
    int32_t& y,
    const char* first_key,
    uint64_t first_value,
    const char* second_key,
    uint64_t second_value) {
    char line[112]{};
    size_t length = 0U;
    append_text(line, sizeof(line), length, first_key);
    append_text(line, sizeof(line), length, "=");
    append_hex(line, sizeof(line), length, first_value);
    append_text(line, sizeof(line), length, "  ");
    append_text(line, sizeof(line), length, second_key);
    append_text(line, sizeof(line), length, "=");
    append_hex(line, sizeof(line), length, second_value);
    static_cast<void>(draw_line(y, kText, line));
}

void render_framebuffer(const FatalSnapshot& snapshot) {
    if (!graphics::available()) {
        return;
    }
    if (graphics::frame_active()) {
        graphics::end_frame();
    }
    graphics::reset_clip();
    graphics::reset_text_scale_limit();
    graphics::clear(kBackground);

    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t header_height = graphics::height() >= 160U ? 42 : 28;
    graphics::fill_rect(0, 0, width, header_height, kRed);
    graphics::draw_text(
        16, header_height >= 42 ? 14 : 9,
        "KUROGANE FATAL DIAGNOSTIC",
        kText, kRed, 1U, false);

    int32_t y = header_height + 12;
    char line[128]{};
    size_t length = 0U;
    append_text(line, sizeof(line), length, "STOP: ");
    append_text(line, sizeof(line), length, snapshot.reason);
    static_cast<void>(draw_line(y, kRedSoft, line));

    line[0] = '\0';
    length = 0U;
    append_text(line, sizeof(line), length, "VECTOR=");
    append_u64(line, sizeof(line), length, snapshot.vector);
    append_text(line, sizeof(line), length, "  ERROR=");
    append_hex(line, sizeof(line), length, snapshot.registers.error_code);
    append_text(line, sizeof(line), length, "  CONTEXT=");
    append_text(line, sizeof(line), length, context_name(snapshot.context));
    static_cast<void>(draw_line(y, kText, line));

    line[0] = '\0';
    length = 0U;
    append_text(line, sizeof(line), length, "CPU=");
    if (snapshot.cpu == events::UNKNOWN_CPU) {
        append_text(line, sizeof(line), length, "UNKNOWN");
    } else {
        append_u64(line, sizeof(line), length, snapshot.cpu);
    }
    append_text(line, sizeof(line), length, "  PID=");
    append_u64(line, sizeof(line), length, snapshot.pid);
    append_text(line, sizeof(line), length, "  TID=");
    append_u64(line, sizeof(line), length, snapshot.tid);
    append_text(line, sizeof(line), length, "  ");
    append_text(line, sizeof(line), length, snapshot.process_name);
    append_text(line, sizeof(line), length, "/");
    append_text(line, sizeof(line), length, snapshot.thread_name);
    static_cast<void>(draw_line(y, kMuted, line));

    draw_pair_hex(y, "RIP", snapshot.registers.rip, "RSP", snapshot.registers.rsp);
    draw_pair_hex(y, "RFLAGS", snapshot.registers.rflags, "RBP", snapshot.registers.rbp);
    if (snapshot.cr2_valid) {
        draw_pair_hex(y, "CR2", snapshot.cr2, "CR3", snapshot.vmm_root);
    }
    draw_pair_hex(y, "RAX", snapshot.registers.rax, "RBX", snapshot.registers.rbx);
    draw_pair_hex(y, "RCX", snapshot.registers.rcx, "RDX", snapshot.registers.rdx);
    draw_pair_hex(y, "RSI", snapshot.registers.rsi, "RDI", snapshot.registers.rdi);
    draw_pair_hex(y, "R8", snapshot.registers.r8, "R9", snapshot.registers.r9);
    draw_pair_hex(y, "R10", snapshot.registers.r10, "R11", snapshot.registers.r11);
    draw_pair_hex(y, "R12", snapshot.registers.r12, "R13", snapshot.registers.r13);
    draw_pair_hex(y, "R14", snapshot.registers.r14, "R15", snapshot.registers.r15);

    if (draw_line(y, kRedSoft, "LAST KERNEL EVENTS")) {
        const size_t first = snapshot.event_count > 5U
            ? snapshot.event_count - 5U
            : 0U;
        for (size_t index = first; index < snapshot.event_count; ++index) {
            const events::Event& event = snapshot.events[index];
            line[0] = '\0';
            length = 0U;
            append_text(line, sizeof(line), length, "[");
            append_u64(line, sizeof(line), length, event.monotonic_tick);
            append_text(line, sizeof(line), length, "] ");
            append_text(line, sizeof(line), length, event.subsystem);
            append_text(line, sizeof(line), length, " ");
            append_text(line, sizeof(line), length, event.message);
            if (!draw_line(y, kMuted, line)) {
                break;
            }
        }
    }

    line[0] = '\0';
    length = 0U;
    append_text(line, sizeof(line), length, "DUMP: ");
    append_text(line, sizeof(line), length, dump_status_message(snapshot.dump_status));
    static_cast<void>(draw_line(y, kRedSoft, line));
    static_cast<void>(draw_line(
        y, kMuted,
        "Essential snapshot mirrored to serial. System halted."));
}

uint32_t checksum32(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t hash = UINT32_C(2166136261);
    for (size_t index = 0U; index < size; ++index) {
        hash ^= bytes[index];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

void attempt_dump(FatalSnapshot& snapshot) {
    PanicSafeDumpWriter writer = __atomic_load_n(&g_dump_writer, __ATOMIC_ACQUIRE);
    if (writer == nullptr) {
        snapshot.dump_status = DumpStatus::UnavailableNoSafeWriter;
        return;
    }

    PanicDump dump{};
    static constexpr uint8_t magic[8] = {
        'K', 'U', 'R', 'O', 'D', 'M', 'P', '1'
    };
    for (size_t index = 0U; index < sizeof(magic); ++index) {
        dump.magic[index] = magic[index];
    }
    dump.format_version = DUMP_FORMAT_VERSION;
    dump.total_size = static_cast<uint32_t>(sizeof(PanicDump));
    dump.snapshot = snapshot;
    dump.checksum = checksum32(&dump, offsetof(PanicDump, checksum));
    snapshot.dump_status = writer(&dump, sizeof(dump))
        ? DumpStatus::Written
        : DumpStatus::WriteFailed;
}

[[noreturn]] void nested_fallback(
    const arch::x86_64::interrupts::InterruptFrame& frame) {
    if (!serial::ready()) {
        static_cast<void>(serial::init());
    }
    serial::write("\n=== KUROGANE_FATAL_NESTED ===\n");
    serial::write("NESTED PANIC: minimal serial fallback\nVECTOR=");
    serial_u64(frame.vector);
    serial::write("\nRIP=");
    serial::write_hex(frame.rip);
    serial::write("\nDUMP UNAVAILABLE: nested panic\n");
    arch::x86_64::interrupts::halt();
}

} // namespace

bool register_panic_safe_dump_writer(PanicSafeDumpWriter writer) {
    if (writer == nullptr) {
        return false;
    }
    PanicSafeDumpWriter expected = nullptr;
    return __atomic_compare_exchange_n(
        &g_dump_writer,
        &expected,
        writer,
        false,
        __ATOMIC_RELEASE,
        __ATOMIC_RELAXED);
}

void unregister_panic_safe_dump_writer(PanicSafeDumpWriter writer) {
    if (writer == nullptr) {
        return;
    }
    PanicSafeDumpWriter expected = writer;
    static_cast<void>(__atomic_compare_exchange_n(
        &g_dump_writer,
        &expected,
        static_cast<PanicSafeDumpWriter>(nullptr),
        false,
        __ATOMIC_RELEASE,
        __ATOMIC_RELAXED));
}

const FatalSnapshot& last_snapshot() {
    return g_snapshot;
}

const char* exception_name(uint8_t vector) {
    static const char* const names[32] = {
        "divide error", "debug", "non-maskable interrupt", "breakpoint",
        "overflow", "bound range exceeded", "invalid opcode",
        "device not available", "double fault", "coprocessor overrun",
        "invalid TSS", "segment not present", "stack-segment fault",
        "general protection fault", "page fault", "reserved",
        "x87 floating-point exception", "alignment check", "machine check",
        "SIMD floating-point exception", "virtualization exception",
        "control-protection exception", "reserved", "reserved", "reserved",
        "reserved", "reserved", "reserved", "hypervisor injection exception",
        "VMM communication exception", "security exception", "reserved",
    };
    return vector < 32U ? names[vector] : "unknown exception";
}

const char* context_name(Context context) {
    switch (context) {
        case Context::Kernel: return "KERNEL";
        case Context::Userspace: return "USERSPACE";
        case Context::Interrupt: return "IRQ";
        case Context::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

const char* dump_status_message(DumpStatus status) {
    switch (status) {
        case DumpStatus::UnavailableNoSafeWriter:
            return "DUMP UNAVAILABLE: no qualified panic-safe storage writer";
        case DumpStatus::Written:
            return "WRITTEN: KURODMP1 checksum included";
        case DumpStatus::WriteFailed:
            return "DUMP UNAVAILABLE: panic-safe writer failed";
    }
    return "DUMP UNAVAILABLE: unknown dump status";
}

[[noreturn]] void fatal_exception(
    arch::x86_64::interrupts::InterruptFrame& frame) {
    arch::x86_64::interrupts::disable();
    if (__atomic_exchange_n(
            &g_panic_depth, UINT32_C(1), __ATOMIC_ACQ_REL) != 0U) {
        nested_fallback(frame);
    }

    capture_snapshot(g_snapshot, frame);
    events::record(
        g_snapshot.monotonic_tick,
        "PANIC",
        events::Severity::Fatal,
        g_snapshot.cpu,
        g_snapshot.pid,
        g_snapshot.tid,
        kEventPanicTransition,
        "fatal exception snapshot captured");

#if defined(KUROGANE_PANIC_TEST) && KUROGANE_PANIC_TEST
    if (g_force_nested_fallback) {
        fatal_exception(frame);
    }
#endif

    attempt_dump(g_snapshot);
    mirror_serial(g_snapshot);
    render_framebuffer(g_snapshot);
    arch::x86_64::interrupts::halt();
}

#if defined(KUROGANE_PANIC_TEST) && KUROGANE_PANIC_TEST
[[noreturn]] void inject_invalid_opcode() {
    const uint64_t tid = threading::current();
    const uint64_t pid = threading::current_process();
    events::record(
        threading::timer_ticks(),
        "PANIC-TEST",
        events::Severity::Warn,
        current_cpu(),
        pid,
        tid,
        kEventFaultInjection,
        "deliberate invalid-opcode fault armed");
    if (!serial::ready()) {
        static_cast<void>(serial::init());
    }
    serial::write("[PANIC-TEST] deliberate invalid opcode now\n");
    __asm__ volatile("ud2");
    __builtin_unreachable();
}

void arm_nested_fallback_for_test() {
    g_force_nested_fallback = true;
}
#endif

} // namespace diagnostics::panic
