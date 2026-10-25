#include "reliable_file.hpp"

namespace install::reliable_file {
namespace {

bool text_equal(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) return false;
    size_t index = 0U;
    while (left[index] == right[index] && left[index] != '\0') ++index;
    return left[index] == right[index];
}

bool valid_operations(const Operations& operations) {
    return operations.stat != nullptr && operations.create != nullptr &&
        operations.write != nullptr && operations.unlink != nullptr &&
        operations.rename != nullptr && operations.sync != nullptr;
}

bool valid_paths(const Paths& paths) {
    if (paths.target == nullptr || paths.staging == nullptr ||
        paths.rollback == nullptr || paths.committed == nullptr ||
        paths.target[0] == '\0' || paths.staging[0] == '\0' ||
        paths.rollback[0] == '\0' || paths.committed[0] == '\0') {
        return false;
    }
    return !text_equal(paths.target, paths.staging) &&
        !text_equal(paths.target, paths.rollback) &&
        !text_equal(paths.target, paths.committed) &&
        !text_equal(paths.staging, paths.rollback) &&
        !text_equal(paths.staging, paths.committed) &&
        !text_equal(paths.rollback, paths.committed);
}

Status exists(
    const Operations& operations,
    const char* path,
    bool* present) {
    if (present == nullptr) return Status::InvalidArgument;
    const BackendStatus status = operations.stat(operations.context, path);
    if (status == BackendStatus::Ok) {
        *present = true;
        return Status::Ok;
    }
    if (status == BackendStatus::NotFound) {
        *present = false;
        return Status::Ok;
    }
    return Status::RecoveryFailed;
}

bool sync_ok(const Operations& operations) {
    return operations.sync(operations.context) == BackendStatus::Ok;
}

Status remove_staging(
    const Operations& operations,
    const Paths& paths) {
    bool staging_present = false;
    Status status = exists(operations, paths.staging, &staging_present);
    if (status != Status::Ok) return status;
    if (!staging_present) return Status::Ok;
    const BackendStatus removed =
        operations.unlink(operations.context, paths.staging);
    if (removed != BackendStatus::Ok && removed != BackendStatus::NotFound) {
        return Status::StagingCleanupFailed;
    }
    return sync_ok(operations)
        ? Status::Ok
        : Status::StagingCleanupFailed;
}

Status restore_rollback(
    const Operations& operations,
    const Paths& paths,
    bool target_may_exist) {
    if (target_may_exist) {
        const BackendStatus removed =
            operations.unlink(operations.context, paths.target);
        if (removed != BackendStatus::Ok && removed != BackendStatus::NotFound) {
            return Status::RollbackFailed;
        }
    }
    const BackendStatus restored = operations.rename(
        operations.context, paths.rollback, paths.target);
    if (restored != BackendStatus::Ok) {
        return Status::RollbackFailed;
    }
    return sync_ok(operations)
        ? Status::Ok
        : Status::RollbackSyncFailed;
}

Status recover_interrupted(
    const Operations& operations,
    const Paths& paths) {
    bool rollback_present = false;
    bool committed_present = false;
    Status status = exists(operations, paths.rollback, &rollback_present);
    if (status != Status::Ok) return status;
    status = exists(operations, paths.committed, &committed_present);
    if (status != Status::Ok) return status;
    if (rollback_present && committed_present) {
        return Status::StateConflict;
    }

    bool target_present = false;
    status = exists(operations, paths.target, &target_present);
    if (status != Status::Ok) return status;

    if (rollback_present) {
        status = restore_rollback(operations, paths, target_present);
        if (status != Status::Ok) return Status::RecoveryFailed;
        target_present = true;
    } else if (committed_present) {
        if (target_present) {
            const BackendStatus removed =
                operations.unlink(operations.context, paths.committed);
            if (removed != BackendStatus::Ok &&
                removed != BackendStatus::NotFound) {
                return Status::RecoveryFailed;
            }
            if (!sync_ok(operations)) return Status::RecoveryFailed;
        } else {
            const BackendStatus restored = operations.rename(
                operations.context, paths.committed, paths.target);
            if (restored != BackendStatus::Ok || !sync_ok(operations)) {
                return Status::RecoveryFailed;
            }
            target_present = true;
        }
    }

    (void)target_present;
    return remove_staging(operations, paths);
}

} // namespace

Status replace(
    const Operations& operations,
    const Paths& paths,
    const void* data,
    size_t size) {
    if (!valid_operations(operations) || !valid_paths(paths) ||
        (data == nullptr && size != 0U)) {
        return Status::InvalidArgument;
    }

    Status status = recover_interrupted(operations, paths);
    if (status != Status::Ok) return status;

    BackendStatus backend =
        operations.create(operations.context, paths.staging);
    if (backend != BackendStatus::Ok) {
        return Status::StagingCreateFailed;
    }
    if (size != 0U) {
        backend = operations.write(
            operations.context, paths.staging, 0U, data, size);
        if (backend != BackendStatus::Ok) {
            return Status::StagingWriteFailed;
        }
    }
    if (!sync_ok(operations)) return Status::StagingSyncFailed;

    bool target_present = false;
    status = exists(operations, paths.target, &target_present);
    if (status != Status::Ok) return status;

    if (target_present) {
        backend = operations.rename(
            operations.context, paths.target, paths.rollback);
        if (backend != BackendStatus::Ok) return Status::BackupRenameFailed;
        if (!sync_ok(operations)) {
            const Status rollback = restore_rollback(
                operations, paths, false);
            return rollback == Status::Ok
                ? Status::BackupSyncFailed
                : rollback;
        }
    }

    backend = operations.rename(
        operations.context, paths.staging, paths.target);
    if (backend != BackendStatus::Ok) {
        if (target_present) {
            const Status rollback = restore_rollback(
                operations, paths, false);
            return rollback == Status::Ok
                ? Status::PublishRenameFailed
                : rollback;
        }
        return Status::PublishRenameFailed;
    }

    if (!sync_ok(operations)) {
        if (target_present) {
            const Status rollback = restore_rollback(
                operations, paths, true);
            return rollback == Status::Ok
                ? Status::PublishSyncFailed
                : rollback;
        }
        return Status::PublishSyncFailed;
    }

    if (!target_present) return Status::Ok;

    backend = operations.rename(
        operations.context, paths.rollback, paths.committed);
    if (backend != BackendStatus::Ok) {
        return Status::CommitMarkerRenameFailed;
    }
    if (!sync_ok(operations)) return Status::CommitMarkerSyncFailed;

    backend = operations.unlink(operations.context, paths.committed);
    if (backend != BackendStatus::Ok && backend != BackendStatus::NotFound) {
        return Status::CleanupFailed;
    }
    return sync_ok(operations) ? Status::Ok : Status::CleanupSyncFailed;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::InvalidArgument: return "invalid argument";
        case Status::StateConflict: return "conflicting recovery artifacts";
        case Status::RecoveryFailed: return "interrupted replacement recovery failed";
        case Status::StagingCleanupFailed: return "stale staging cleanup failed";
        case Status::StagingCreateFailed: return "staging file creation failed";
        case Status::StagingWriteFailed: return "staging file write failed";
        case Status::StagingSyncFailed: return "staging durability sync failed";
        case Status::BackupRenameFailed: return "rollback backup creation failed";
        case Status::BackupSyncFailed: return "rollback backup durability sync failed";
        case Status::PublishRenameFailed: return "staging publish rename failed";
        case Status::PublishSyncFailed: return "published file durability sync failed";
        case Status::CommitMarkerRenameFailed: return "commit-state rename failed";
        case Status::CommitMarkerSyncFailed: return "commit-state sync failed";
        case Status::CleanupFailed: return "committed backup cleanup failed";
        case Status::CleanupSyncFailed: return "committed backup cleanup sync failed";
        case Status::RollbackFailed: return "rollback failed";
        case Status::RollbackSyncFailed: return "rollback sync failed";
    }
    return "unknown reliable-file status";
}

} // namespace install::reliable_file
