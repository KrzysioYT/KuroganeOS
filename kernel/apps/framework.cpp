#include "framework.hpp"

#include "../core/string.hpp"

namespace applications {

namespace {
Definition g_applications[MAX_APPLICATIONS]{};
size_t g_application_count = 0;
size_t g_active = MAX_APPLICATIONS;
bool g_initialized = false;

bool valid_name(const char* name) {
    const size_t length = kstd::strnlen(name, 33);
    return length > 0 && length <= 32;
}

size_t find_index(const char* name) {
    if (!name) {
        return MAX_APPLICATIONS;
    }
    for (size_t i = 0; i < g_application_count; ++i) {
        if (kstd::streq(g_applications[i].name, name)) {
            return i;
        }
    }
    return MAX_APPLICATIONS;
}
} // namespace

void initialize() {
    g_application_count = 0;
    g_active = MAX_APPLICATIONS;
    g_initialized = true;
}

Status register_application(const Definition& definition) {
    if (!g_initialized) {
        initialize();
    }
    if (!definition.name || !definition.description || !definition.start) {
        return Status::InvalidArgument;
    }
    if (!valid_name(definition.name)) {
        return Status::NameTooLong;
    }
    if (find_index(definition.name) != MAX_APPLICATIONS) {
        return Status::Duplicate;
    }
    if (g_application_count >= MAX_APPLICATIONS) {
        return Status::CapacityReached;
    }
    g_applications[g_application_count++] = definition;
    return Status::Ok;
}

Status launch(const char* name, const char* arguments) {
    if (running()) {
        return Status::Busy;
    }
    const size_t index = find_index(name);
    if (index == MAX_APPLICATIONS) {
        return Status::NotFound;
    }
    g_active = index;
    if (!g_applications[index].start(arguments ? arguments : "")) {
        g_active = MAX_APPLICATIONS;
        return Status::StartFailed;
    }
    return Status::Ok;
}

Status stop() {
    if (!running()) {
        return Status::NotRunning;
    }
    const size_t previous = g_active;
    g_active = MAX_APPLICATIONS;
    if (g_applications[previous].stop) {
        g_applications[previous].stop();
    }
    return Status::Ok;
}

bool running() {
    return g_active < g_application_count;
}

const char* active_name() {
    return running() ? g_applications[g_active].name : nullptr;
}

void dispatch_key(char character) {
    if (running() && g_applications[g_active].key) {
        g_applications[g_active].key(character);
    }
}

void dispatch_tick(uint64_t tick) {
    if (running() && g_applications[g_active].tick) {
        g_applications[g_active].tick(tick);
    }
}

void list(ListCallback callback, void* context) {
    if (!callback) {
        return;
    }
    for (size_t i = 0; i < g_application_count; ++i) {
        if (!callback(g_applications[i], context)) {
            return;
        }
    }
}

const char* status_message(Status status) {
    switch (status) {
    case Status::Ok: return "ok";
    case Status::InvalidArgument: return "invalid argument";
    case Status::NameTooLong: return "application name too long";
    case Status::Duplicate: return "application already registered";
    case Status::CapacityReached: return "application table full";
    case Status::NotFound: return "application not found";
    case Status::Busy: return "another application is running";
    case Status::NotRunning: return "no application is running";
    case Status::StartFailed: return "application failed to start";
    case Status::IterationStopped: return "iteration stopped";
    }
    return "unknown error";
}

} // namespace applications
