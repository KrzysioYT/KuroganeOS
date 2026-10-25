#pragma once

#include <stdint.h>

#include "../storage/block_device.hpp"
#include "kurofs.hpp"
#include "vfs.hpp"

namespace fs::kurofs_volume {

enum class Status : uint8_t {
    Ok = 0,
    AlreadyMounted,
    InvalidArgument,
    RootUnavailable,
    KurofsMountFailed,
    AdapterFailed,
    VfsMountFailed,
};

// Mount a complete raw KuroFS block device at /kuro through the production
// root VFS. GPT-contained data volumes can reuse this adapter after partition
// discovery is added; no formatting is ever performed implicitly.
Status mount(const storage::block::Device* device);

bool mounted();
Status status();
kurofs::Status kurofs_detail_status();
vfs::Status vfs_detail_status();
const char* status_message(Status status);

} // namespace fs::kurofs_volume
