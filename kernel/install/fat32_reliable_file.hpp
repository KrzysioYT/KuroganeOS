#pragma once

#include "reliable_file.hpp"
#include "../fs/fat32.hpp"

namespace install::fat32_reliable_file {

reliable_file::Status replace(
    fs::fat32::FileSystem* filesystem,
    const reliable_file::Paths& paths,
    const void* data,
    size_t size);

} // namespace install::fat32_reliable_file
