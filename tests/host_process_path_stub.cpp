#include "../kernel/task/process.hpp"

// The production root-volume image validator intentionally links the real
// FAT32/VFS/root_volume implementation without the scheduler or Ring-3 runtime.
// Provide the process-path hooks expected by root_volume and model the same
// condition as a kernel caller outside any process: no current PID, therefore
// the root/default VFS PathContext is selected.
namespace process {

ProcessId current() {
    return INVALID_PROCESS_ID;
}

Status stat(ProcessId, Stat* output) {
    if (output != nullptr) *output = {};
    return Status::NotFound;
}

Status set_working_directory(ProcessId, const char*) {
    return Status::NotFound;
}

} // namespace process
