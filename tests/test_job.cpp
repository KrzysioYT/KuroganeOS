#include "../kernel/task/job.hpp"

#include <cassert>
#include <cstring>

int main() {
    using namespace process::job;

    assert(initialize() == Status::Ok);
    assert(initialize() == Status::AlreadyInitialized);

    JobId job = INVALID_JOB_ID;
    assert(create(10U, "session", &job) == Status::Ok);
    assert(job != INVALID_JOB_ID);
    assert(contains(job, 10U));

    assert(attach(99U, job, 11U) == Status::AccessDenied);
    assert(attach(10U, job, 11U) == Status::Ok);
    assert(attach(10U, job, 11U) == Status::AlreadyExists);
    assert(inherit_member(job, 12U) == Status::Ok);

    Stat stat_value{};
    assert(stat(job, &stat_value) == Status::Ok);
    assert(stat_value.owner_pid == 10U);
    assert(stat_value.member_count == 3U);
    assert(!stat_value.orphaned);
    assert(std::strcmp(stat_value.name, "session") == 0);

    release_process(10U);
    assert(stat(job, &stat_value) == Status::Ok);
    assert(stat_value.owner_pid == INVALID_PROCESS_ID);
    assert(stat_value.orphaned);
    assert(stat_value.member_count == 2U);

    release_process(11U);
    release_process(12U);
    assert(stat(job, &stat_value) == Status::NotFound);

    JobId replacement = INVALID_JOB_ID;
    assert(create(20U, "replacement", &replacement) == Status::Ok);
    assert(replacement != job);
    assert(stat(job, &stat_value) == Status::NotFound);
    return 0;
}
