#pragma once

#include <stddef.h>
#include <stdint.h>

namespace applications {

constexpr size_t MAX_APPLICATIONS = 16;

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    NameTooLong,
    Duplicate,
    CapacityReached,
    NotFound,
    Busy,
    NotRunning,
    StartFailed,
    IterationStopped
};

using StartCallback = bool (*)(const char* arguments);
using KeyCallback = void (*)(char character);
using TickCallback = void (*)(uint64_t tick);
using StopCallback = void (*)();

struct Definition {
    const char* name;
    const char* description;
    StartCallback start;
    KeyCallback key;
    TickCallback tick;
    StopCallback stop;
};

using ListCallback = bool (*)(const Definition& application, void* context);

void initialize();
Status register_application(const Definition& definition);
Status launch(const char* name, const char* arguments = nullptr);
Status stop();
bool running();
const char* active_name();
void dispatch_key(char character);
void dispatch_tick(uint64_t tick);
void list(ListCallback callback, void* context);
const char* status_message(Status status);

} // namespace applications
