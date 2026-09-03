#include "../kernel/install/reliable_file.hpp"

#include <cstdint>
#include <iostream>
#include <map>
#include <string>

namespace {

using install::reliable_file::BackendStatus;
using install::reliable_file::Operations;
using install::reliable_file::Paths;
using install::reliable_file::Status;

int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                      \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": "       \
                      << #condition << '\n';                                    \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

struct FakeBackend {
    std::map<std::string, std::string> files;
    std::string fail_write_path;
    std::string fail_rename_source;
    std::string fail_rename_destination;
    size_t sync_calls = 0U;
    size_t fail_sync_call = 0U;
};

BackendStatus fake_stat(void* context, const char* path) {
    auto* backend = static_cast<FakeBackend*>(context);
    if (backend == nullptr || path == nullptr) return BackendStatus::Failure;
    return backend->files.find(path) == backend->files.end()
        ? BackendStatus::NotFound
        : BackendStatus::Ok;
}

BackendStatus fake_create(void* context, const char* path) {
    auto* backend = static_cast<FakeBackend*>(context);
    if (backend == nullptr || path == nullptr) return BackendStatus::Failure;
    if (backend->files.find(path) != backend->files.end()) {
        return BackendStatus::AlreadyExists;
    }
    backend->files[path] = {};
    return BackendStatus::Ok;
}

BackendStatus fake_write(
    void* context,
    const char* path,
    uint64_t offset,
    const void* data,
    size_t size) {
    auto* backend = static_cast<FakeBackend*>(context);
    if (backend == nullptr || path == nullptr ||
        (data == nullptr && size != 0U) || offset != 0U) {
        return BackendStatus::Failure;
    }
    if (backend->fail_write_path == path) return BackendStatus::Failure;
    auto entry = backend->files.find(path);
    if (entry == backend->files.end()) return BackendStatus::NotFound;
    entry->second.assign(static_cast<const char*>(data), size);
    return BackendStatus::Ok;
}

BackendStatus fake_unlink(void* context, const char* path) {
    auto* backend = static_cast<FakeBackend*>(context);
    if (backend == nullptr || path == nullptr) return BackendStatus::Failure;
    const auto entry = backend->files.find(path);
    if (entry == backend->files.end()) return BackendStatus::NotFound;
    backend->files.erase(entry);
    return BackendStatus::Ok;
}

BackendStatus fake_rename(
    void* context,
    const char* source_path,
    const char* destination_path) {
    auto* backend = static_cast<FakeBackend*>(context);
    if (backend == nullptr || source_path == nullptr ||
        destination_path == nullptr) {
        return BackendStatus::Failure;
    }
    if (backend->fail_rename_source == source_path &&
        backend->fail_rename_destination == destination_path) {
        return BackendStatus::Failure;
    }
    const auto source = backend->files.find(source_path);
    if (source == backend->files.end()) return BackendStatus::NotFound;
    if (backend->files.find(destination_path) != backend->files.end()) {
        return BackendStatus::AlreadyExists;
    }
    const std::string contents = source->second;
    backend->files.erase(source);
    backend->files[destination_path] = contents;
    return BackendStatus::Ok;
}

BackendStatus fake_sync(void* context) {
    auto* backend = static_cast<FakeBackend*>(context);
    if (backend == nullptr) return BackendStatus::Failure;
    ++backend->sync_calls;
    if (backend->fail_sync_call != 0U &&
        backend->sync_calls == backend->fail_sync_call) {
        return BackendStatus::Failure;
    }
    return BackendStatus::Ok;
}

Operations make_operations(FakeBackend* backend) {
    return {
        backend,
        fake_stat,
        fake_create,
        fake_write,
        fake_unlink,
        fake_rename,
        fake_sync,
    };
}

constexpr Paths kPaths{
    "/etc/user.cfg",
    "/etc/user.new",
    "/etc/user.bak",
    "/etc/user.old",
};

bool has(const FakeBackend& backend, const char* path) {
    return backend.files.find(path) != backend.files.end();
}

std::string value(const FakeBackend& backend, const char* path) {
    const auto entry = backend.files.find(path);
    return entry == backend.files.end() ? std::string{} : entry->second;
}

void check_clean_commit(const FakeBackend& backend, const char* expected) {
    CHECK(has(backend, kPaths.target));
    CHECK(value(backend, kPaths.target) == expected);
    CHECK(!has(backend, kPaths.staging));
    CHECK(!has(backend, kPaths.rollback));
    CHECK(!has(backend, kPaths.committed));
}

void test_successful_replace_preserves_transaction_order() {
    FakeBackend backend{};
    backend.files[kPaths.target] = "old-profile";
    const char replacement[] = "new-profile";
    const Status status = install::reliable_file::replace(
        make_operations(&backend), kPaths, replacement, sizeof(replacement) - 1U);
    CHECK(status == Status::Ok);
    check_clean_commit(backend, "new-profile");
    CHECK(backend.sync_calls == 5U);
}

void test_staging_write_failure_keeps_old_target() {
    FakeBackend backend{};
    backend.files[kPaths.target] = "old-profile";
    backend.fail_write_path = kPaths.staging;
    const char replacement[] = "new-profile";
    const Status status = install::reliable_file::replace(
        make_operations(&backend), kPaths, replacement, sizeof(replacement) - 1U);
    CHECK(status == Status::StagingWriteFailed);
    CHECK(value(backend, kPaths.target) == "old-profile");
    CHECK(!has(backend, kPaths.rollback));
    CHECK(!has(backend, kPaths.committed));
}

void test_publish_rename_failure_rolls_back_old_target() {
    FakeBackend backend{};
    backend.files[kPaths.target] = "old-profile";
    backend.fail_rename_source = kPaths.staging;
    backend.fail_rename_destination = kPaths.target;
    const char replacement[] = "new-profile";
    const Status status = install::reliable_file::replace(
        make_operations(&backend), kPaths, replacement, sizeof(replacement) - 1U);
    CHECK(status == Status::PublishRenameFailed);
    CHECK(value(backend, kPaths.target) == "old-profile");
    CHECK(has(backend, kPaths.staging));
    CHECK(!has(backend, kPaths.rollback));
}

void test_publish_sync_failure_rolls_back_old_target() {
    FakeBackend backend{};
    backend.files[kPaths.target] = "old-profile";
    // stage sync=1, rollback-backup sync=2, published-target sync=3
    backend.fail_sync_call = 3U;
    const char replacement[] = "new-profile";
    const Status status = install::reliable_file::replace(
        make_operations(&backend), kPaths, replacement, sizeof(replacement) - 1U);
    CHECK(status == Status::PublishSyncFailed);
    CHECK(value(backend, kPaths.target) == "old-profile");
    CHECK(!has(backend, kPaths.rollback));
    CHECK(!has(backend, kPaths.committed));
}

void test_interrupted_uncommitted_state_is_recovered_before_replace() {
    FakeBackend backend{};
    backend.files[kPaths.target] = "uncommitted-new";
    backend.files[kPaths.rollback] = "known-good-old";
    backend.files[kPaths.staging] = "stale-stage";
    const char replacement[] = "fresh-profile";
    const Status status = install::reliable_file::replace(
        make_operations(&backend), kPaths, replacement, sizeof(replacement) - 1U);
    CHECK(status == Status::Ok);
    check_clean_commit(backend, "fresh-profile");
}

void test_interrupted_committed_state_keeps_new_target() {
    FakeBackend backend{};
    backend.files[kPaths.target] = "durable-new";
    backend.files[kPaths.committed] = "retired-old";
    const char replacement[] = "next-profile";
    const Status status = install::reliable_file::replace(
        make_operations(&backend), kPaths, replacement, sizeof(replacement) - 1U);
    CHECK(status == Status::Ok);
    check_clean_commit(backend, "next-profile");
}

void test_conflicting_artifacts_fail_closed() {
    FakeBackend backend{};
    backend.files[kPaths.target] = "target";
    backend.files[kPaths.rollback] = "old-a";
    backend.files[kPaths.committed] = "old-b";
    const char replacement[] = "new";
    const Status status = install::reliable_file::replace(
        make_operations(&backend), kPaths, replacement, sizeof(replacement) - 1U);
    CHECK(status == Status::StateConflict);
    CHECK(value(backend, kPaths.target) == "target");
    CHECK(value(backend, kPaths.rollback) == "old-a");
    CHECK(value(backend, kPaths.committed) == "old-b");
}

} // namespace

int main() {
    test_successful_replace_preserves_transaction_order();
    test_staging_write_failure_keeps_old_target();
    test_publish_rename_failure_rolls_back_old_target();
    test_publish_sync_failure_rolls_back_old_target();
    test_interrupted_uncommitted_state_is_recovered_before_replace();
    test_interrupted_committed_state_keeps_new_target();
    test_conflicting_artifacts_fail_closed();

    if (failures != 0) {
        std::cerr << failures << " reliable-file test(s) failed\n";
        return 1;
    }
    std::cout << "reliable-file tests passed\n";
    return 0;
}
