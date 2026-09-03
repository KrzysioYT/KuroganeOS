#include "kurofs_volume.hpp"

#include "kurofs_vfs.hpp"
#include "root_volume.hpp"

namespace fs::kurofs_volume {
namespace {

Status g_status = Status::InvalidArgument;
kurofs::Status g_kurofs_detail = kurofs::Status::Ok;
vfs::Status g_vfs_detail = vfs::Status::Ok;
kurofs::FileSystem g_filesystem{};
kurofs_vfs::Adapter g_adapter{};
vfs::FileSystem g_backend{};
vfs::MountHandle g_mount{};
bool g_mounted = false;

} // namespace

Status mount(const storage::block::Device* device) {
    if (g_mounted) return Status::AlreadyMounted;
    if (device == nullptr) {
        g_status = Status::InvalidArgument;
        return g_status;
    }
    if (!root_volume::mounted()) {
        g_status = Status::RootUnavailable;
        return g_status;
    }

    g_filesystem = {};
    g_adapter = {};
    g_backend = {};
    g_mount = {};
    g_kurofs_detail = kurofs::mount(&g_filesystem, device);
    if (g_kurofs_detail != kurofs::Status::Ok) {
        g_status = Status::KurofsMountFailed;
        return g_status;
    }
    g_vfs_detail = kurofs_vfs::initialize(
        &g_adapter, &g_filesystem, &g_backend);
    if (g_vfs_detail != vfs::Status::Ok) {
        g_status = Status::AdapterFailed;
        return g_status;
    }
    g_vfs_detail = root_volume::mount_backend(
        "/kuro", &g_backend, &g_mount);
    if (g_vfs_detail != vfs::Status::Ok) {
        g_status = Status::VfsMountFailed;
        return g_status;
    }
    g_mounted = true;
    g_status = Status::Ok;
    return g_status;
}

bool mounted() { return g_mounted; }
Status status() { return g_status; }
kurofs::Status kurofs_detail_status() { return g_kurofs_detail; }
vfs::Status vfs_detail_status() { return g_vfs_detail; }

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyMounted: return "KuroFS volume already mounted";
        case Status::InvalidArgument: return "invalid KuroFS volume argument";
        case Status::RootUnavailable: return "persistent root VFS unavailable";
        case Status::KurofsMountFailed: return "raw KuroFS mount failed";
        case Status::AdapterFailed: return "KuroFS VFS adapter failed";
        case Status::VfsMountFailed: return "KuroFS /kuro mount failed";
    }
    return "unknown KuroFS volume status";
}

} // namespace fs::kurofs_volume
