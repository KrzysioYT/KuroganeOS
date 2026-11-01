#pragma once

#include "package.hpp"
#include "../fs/vfs.hpp"

namespace install::package_vfs {

struct Adapter {
    package::View package;
    package::File files[package::MAXIMUM_FILES]{};
};

fs::vfs::Status initialize(
    Adapter* adapter,
    const package::View& package,
    fs::vfs::FileSystem* filesystem);

} // namespace install::package_vfs
