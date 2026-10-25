#pragma once

#include <stddef.h>
#include <stdint.h>

namespace install::reliable_file {

enum class BackendStatus : uint8_t {
    Ok = 0,
    NotFound,
    AlreadyExists,
    Failure,
};

struct Operations {
    void* context;
    BackendStatus (*stat)(void* context, const char* path);
    BackendStatus (*create)(void* context, const char* path);
    BackendStatus (*write)(
        void* context,
        const char* path,
        uint64_t offset,
        const void* data,
        size_t size);
    BackendStatus (*unlink)(void* context, const char* path);
    BackendStatus (*rename)(
        void* context,
        const char* source_path,
        const char* destination_path);
    BackendStatus (*sync)(void* context);
};

struct Paths {
    const char* target;
    const char* staging;
    const char* rollback;
    const char* committed;
};

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    StateConflict,
    RecoveryFailed,
    StagingCleanupFailed,
    StagingCreateFailed,
    StagingWriteFailed,
    StagingSyncFailed,
    BackupRenameFailed,
    BackupSyncFailed,
    PublishRenameFailed,
    PublishSyncFailed,
    CommitMarkerRenameFailed,
    CommitMarkerSyncFailed,
    CleanupFailed,
    CleanupSyncFailed,
    RollbackFailed,
    RollbackSyncFailed,
};

// Replace a small state file without deleting the last known-good copy first.
//
// Required backend semantics:
// - rename is same-filesystem and refuses to overwrite an existing destination;
// - successful sync makes all preceding metadata/data writes durable;
// - mutating operations report failure without inventing success.
//
// Transaction states:
//   staging   = complete new data, not published yet
//   rollback  = previous target; new target is not committed yet
//   committed = previous target after the new target was durably published
//
// Recovery is conservative: a rollback artifact wins over an uncommitted
// target, while a committed artifact means the current target already passed
// its durability barrier and the old copy can be retired.
Status replace(
    const Operations& operations,
    const Paths& paths,
    const void* data,
    size_t size);

const char* status_message(Status status);

} // namespace install::reliable_file
