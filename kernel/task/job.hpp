#pragma once

#include <stddef.h>
#include <stdint.h>

namespace process::job {

using ProcessId = uint64_t;
using JobId = uint64_t;

constexpr ProcessId INVALID_PROCESS_ID = 0U;
constexpr JobId INVALID_JOB_ID = 0U;
constexpr size_t MAX_JOBS = 16U;
constexpr size_t MAX_JOB_MEMBERS = 16U;
constexpr size_t MAX_JOB_NAME = 31U;

enum class Status : uint8_t {
    Ok = 0,
    NotInitialized,
    AlreadyInitialized,
    InvalidArgument,
    NameTooLong,
    AlreadyExists,
    NotFound,
    AccessDenied,
    CapacityReached
};

struct Stat {
    JobId id;
    ProcessId owner_pid;
    size_t member_count;
    bool orphaned;
    char name[MAX_JOB_NAME + 1U];
};

Status initialize();
Status create(ProcessId owner_pid, const char* name, JobId* out_job);
Status attach(ProcessId owner_pid, JobId job, ProcessId target_pid);
Status detach(ProcessId owner_pid, JobId job, ProcessId target_pid);
Status inherit_member(JobId job, ProcessId child_pid);
void release_process(ProcessId pid);
Status stat(JobId job, Stat* out_stat);
bool contains(JobId job, ProcessId pid);
const char* status_message(Status status);

} // namespace process::job
