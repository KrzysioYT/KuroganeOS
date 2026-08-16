#include "vfs.hpp"

namespace fs::vfs {

namespace {

constexpr uint32_t kKnownNodeFlags =
    static_cast<uint32_t>(NodeFlags::Seekable);
constexpr uint32_t kKnownOpenFlags =
    static_cast<uint32_t>(OpenFlags::Read) |
    static_cast<uint32_t>(OpenFlags::Write) |
    static_cast<uint32_t>(OpenFlags::Append) |
    static_cast<uint32_t>(OpenFlags::Directory);

struct NormalizedPath {
    char value[MAX_PATH_LENGTH + 1];
    size_t length;
    bool trailing_separator;
    bool terminal_special;
};

struct Route {
    size_t mount_index;
    char backend_path[MAX_PATH_LENGTH + 1];
};

void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = static_cast<uint8_t*>(destination);
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = 0;
    }
}

void copy_bytes(void* destination, const void* source, size_t size) {
    uint8_t* output = static_cast<uint8_t*>(destination);
    const uint8_t* input = static_cast<const uint8_t*>(source);
    for (size_t i = 0; i < size; ++i) {
        output[i] = input[i];
    }
}

bool bytes_equal(const char* left, const char* right, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }
    return true;
}

Status bounded_length(const char* text, size_t* length) {
    if (!text || !length) {
        return Status::InvalidArgument;
    }
    for (size_t i = 0; i <= MAX_PATH_LENGTH; ++i) {
        if (text[i] == '\0') {
            *length = i;
            return Status::Ok;
        }
    }
    return Status::PathTooLong;
}

bool component_equals(
    const char* component,
    size_t component_length,
    const char* literal,
    size_t literal_length) {
    return component_length == literal_length &&
           bytes_equal(component, literal, component_length);
}

bool path_equals(
    const char* left,
    size_t left_length,
    const char* right,
    size_t right_length) {
    return left_length == right_length &&
           bytes_equal(left, right, left_length);
}

bool path_is_component_prefix(
    const char* prefix,
    size_t prefix_length,
    const char* path,
    size_t path_length) {
    if (prefix_length == 1 && prefix[0] == '/') {
        return path_length >= 1 && path[0] == '/';
    }
    if (prefix_length > path_length ||
        !bytes_equal(prefix, path, prefix_length)) {
        return false;
    }
    return prefix_length == path_length || path[prefix_length] == '/';
}

bool valid_backend_node_type(NodeType type) {
    return type == NodeType::Regular || type == NodeType::Directory ||
           type == NodeType::Device || type == NodeType::Pipe;
}

bool valid_backend_stat(const FileStat& info) {
    return valid_backend_node_type(info.type) &&
           (static_cast<uint32_t>(info.flags) & ~kKnownNodeFlags) == 0;
}

bool directory_like(NodeType type) {
    return type == NodeType::Directory || type == NodeType::MountPoint;
}

bool valid_status(Status status) {
    return static_cast<uint8_t>(status) <=
           static_cast<uint8_t>(Status::BackendFailure);
}

Status backend_status(Status status) {
    return valid_status(status) ? status : Status::BackendFailure;
}

bool valid_filesystem(const FileSystem& filesystem) {
    const Operations& operations = filesystem.operations;
    if (!operations.stat_path) {
        return false;
    }
    if ((operations.open == nullptr) != (operations.close == nullptr)) {
        return false;
    }
    if (!operations.open &&
        (operations.read || operations.write || operations.stat_open ||
         operations.readdir)) {
        return false;
    }
    return true;
}

Status validate_canonical_global(
    const char* path,
    size_t* path_length,
    size_t* path_depth) {
    size_t length = 0;
    Status status = bounded_length(path, &length);
    if (status != Status::Ok) {
        return status;
    }
    if (length == 0 || path[0] != '/' ||
        (length > 1 && path[length - 1] == '/')) {
        return Status::InvalidPath;
    }

    size_t depth = 0;
    size_t position = 1;
    while (position < length) {
        const size_t begin = position;
        while (position < length && path[position] != '/') {
            ++position;
        }
        const size_t component_length = position - begin;
        if (component_length == 0 || component_length > MAX_NAME_LENGTH ||
            component_equals(path + begin, component_length, ".", 1) ||
            component_equals(path + begin, component_length, "..", 2)) {
            return Status::InvalidPath;
        }
        if (depth == MAX_PATH_DEPTH) {
            return Status::PathTooDeep;
        }
        ++depth;
        if (position < length) {
            ++position;
        }
    }

    if (path_length) {
        *path_length = length;
    }
    if (path_depth) {
        *path_depth = depth;
    }
    return Status::Ok;
}

Status validate_context(
    const PathContext* context,
    size_t* root_length = nullptr,
    size_t* root_depth = nullptr,
    size_t* cwd_length = nullptr) {
    if (!context) {
        return Status::InvalidArgument;
    }

    size_t local_root_length = 0;
    size_t local_root_depth = 0;
    Status status = validate_canonical_global(
        context->root, &local_root_length, &local_root_depth);
    if (status != Status::Ok) {
        return Status::InvalidPath;
    }
    size_t local_cwd_length = 0;
    status = validate_canonical_global(context->cwd, &local_cwd_length, nullptr);
    if (status != Status::Ok ||
        !path_is_component_prefix(
            context->root,
            local_root_length,
            context->cwd,
            local_cwd_length)) {
        return Status::InvalidPath;
    }

    if (root_length) {
        *root_length = local_root_length;
    }
    if (root_depth) {
        *root_depth = local_root_depth;
    }
    if (cwd_length) {
        *cwd_length = local_cwd_length;
    }
    return Status::Ok;
}

Status append_component(
    NormalizedPath* output,
    size_t* depth,
    size_t component_bases[MAX_PATH_DEPTH],
    const char* component,
    size_t component_length) {
    if (*depth == MAX_PATH_DEPTH) {
        return Status::PathTooDeep;
    }
    if (component_length > MAX_NAME_LENGTH) {
        return Status::NameTooLong;
    }

    const size_t separator = output->length == 1 ? 0 : 1;
    if (output->length > MAX_PATH_LENGTH - separator ||
        component_length >
            MAX_PATH_LENGTH - output->length - separator) {
        return Status::PathTooLong;
    }

    component_bases[*depth] = output->length;
    if (separator != 0) {
        output->value[output->length++] = '/';
    }
    copy_bytes(output->value + output->length, component, component_length);
    output->length += component_length;
    output->value[output->length] = '\0';
    ++(*depth);
    return Status::Ok;
}

Status append_canonical_path(
    NormalizedPath* output,
    size_t* depth,
    size_t component_bases[MAX_PATH_DEPTH],
    const char* path,
    size_t path_length) {
    size_t position = 1;
    while (position < path_length) {
        const size_t begin = position;
        while (position < path_length && path[position] != '/') {
            ++position;
        }
        Status status = append_component(
            output,
            depth,
            component_bases,
            path + begin,
            position - begin);
        if (status != Status::Ok) {
            return status;
        }
        if (position < path_length) {
            ++position;
        }
    }
    return Status::Ok;
}

Status normalize_path(
    const PathContext* context,
    const char* path,
    NormalizedPath* output) {
    if (!path || !output) {
        return Status::InvalidArgument;
    }

    size_t root_length = 0;
    size_t root_depth = 0;
    size_t cwd_length = 0;
    Status status = validate_context(
        context, &root_length, &root_depth, &cwd_length);
    if (status != Status::Ok) {
        return status;
    }

    size_t input_length = 0;
    status = bounded_length(path, &input_length);
    if (status != Status::Ok) {
        return status;
    }
    if (input_length == 0) {
        return Status::InvalidPath;
    }

    NormalizedPath candidate = {};
    candidate.value[0] = '/';
    candidate.value[1] = '\0';
    candidate.length = 1;
    candidate.trailing_separator = path[input_length - 1] == '/';
    candidate.terminal_special = false;
    size_t component_bases[MAX_PATH_DEPTH] = {};
    size_t depth = 0;

    const bool absolute = path[0] == '/';
    const char* base = absolute ? context->root : context->cwd;
    const size_t base_length = absolute ? root_length : cwd_length;
    status = append_canonical_path(
        &candidate, &depth, component_bases, base, base_length);
    if (status != Status::Ok) {
        return status;
    }

    size_t position = 0;
    while (position < input_length) {
        while (position < input_length && path[position] == '/') {
            ++position;
        }
        if (position == input_length) {
            break;
        }

        const size_t begin = position;
        while (position < input_length && path[position] != '/') {
            ++position;
        }
        const size_t component_length = position - begin;
        if (component_length > MAX_NAME_LENGTH) {
            return Status::NameTooLong;
        }

        const bool is_dot =
            component_equals(path + begin, component_length, ".", 1);
        const bool is_dot_dot =
            component_equals(path + begin, component_length, "..", 2);
        candidate.terminal_special = is_dot || is_dot_dot;
        if (is_dot) {
            continue;
        }
        if (is_dot_dot) {
            if (depth > root_depth) {
                --depth;
                candidate.length = component_bases[depth];
                candidate.value[candidate.length] = '\0';
            }
            continue;
        }

        status = append_component(
            &candidate,
            &depth,
            component_bases,
            path + begin,
            component_length);
        if (status != Status::Ok) {
            return status;
        }
    }

    *output = candidate;
    return Status::Ok;
}

bool exact_non_root_mount(const State* state, const NormalizedPath& path) {
    for (size_t i = 1; i < MAX_MOUNTS; ++i) {
        const Mount& mount_entry = state->mounts[i];
        if (mount_entry.active &&
            path_equals(
                mount_entry.path,
                mount_entry.path_length,
                path.value,
                path.length)) {
            return true;
        }
    }
    return false;
}

Status route_path(
    const State* state,
    const NormalizedPath& path,
    Route* route) {
    if (!state || !state->initialized || !route) {
        return Status::NotInitialized;
    }

    size_t selected = MAX_MOUNTS;
    size_t longest = 0;
    for (size_t i = 0; i < MAX_MOUNTS; ++i) {
        const Mount& mount_entry = state->mounts[i];
        if (!mount_entry.active ||
            !path_is_component_prefix(
                mount_entry.path,
                mount_entry.path_length,
                path.value,
                path.length)) {
            continue;
        }
        if (selected == MAX_MOUNTS || mount_entry.path_length > longest) {
            selected = i;
            longest = mount_entry.path_length;
        }
    }
    if (selected == MAX_MOUNTS) {
        return Status::NoRootFilesystem;
    }

    Route candidate = {};
    candidate.mount_index = selected;
    const Mount& mount_entry = state->mounts[selected];
    if (path.length == mount_entry.path_length) {
        candidate.backend_path[0] = '/';
        candidate.backend_path[1] = '\0';
    } else if (mount_entry.path_length == 1) {
        copy_bytes(candidate.backend_path, path.value, path.length + 1);
    } else {
        const size_t suffix_length = path.length - mount_entry.path_length;
        copy_bytes(
            candidate.backend_path,
            path.value + mount_entry.path_length,
            suffix_length);
        candidate.backend_path[suffix_length] = '\0';
    }

    *route = candidate;
    return Status::Ok;
}

Status lookup_canonical(
    State* state,
    const NormalizedPath& path,
    Vnode* vnode) {
    Route route = {};
    Status status = route_path(state, path, &route);
    if (status != Status::Ok) {
        return status;
    }

    Mount& mount_entry = state->mounts[route.mount_index];
    FileStat backend_info = {};
    status = backend_status(mount_entry.filesystem.operations.stat_path(
        mount_entry.filesystem.context,
        route.backend_path,
        &backend_info));
    if (status != Status::Ok) {
        return status;
    }
    if (!valid_backend_stat(backend_info)) {
        return Status::BackendFailure;
    }
    if (path.trailing_separator && backend_info.type != NodeType::Directory) {
        return Status::NotDirectory;
    }

    Vnode candidate = {};
    candidate.mount.slot = static_cast<uint16_t>(route.mount_index + 1);
    candidate.mount.generation = mount_entry.generation;
    copy_bytes(
        candidate.backend_path,
        route.backend_path,
        MAX_PATH_LENGTH + 1);
    candidate.info = backend_info;
    if (route.mount_index != 0 &&
        path.length == mount_entry.path_length) {
        if (backend_info.type != NodeType::Directory) {
            return Status::BackendFailure;
        }
        candidate.info.type = NodeType::MountPoint;
    }

    if (vnode) {
        *vnode = candidate;
    }
    return Status::Ok;
}

void reset_state(State* state) {
    clear_bytes(state, sizeof(*state));
}

bool next_generation(uint32_t current, uint32_t* next) {
    if (!next || current == UINT32_MAX) {
        return false;
    }
    *next = current + 1;
    return *next != 0;
}

bool find_mount_slot(
    const State* state,
    size_t* slot,
    uint32_t* generation) {
    for (size_t i = 1; i < MAX_MOUNTS; ++i) {
        if (state->mounts[i].active) {
            continue;
        }
        uint32_t candidate_generation = 0;
        if (!next_generation(
                state->mounts[i].generation,
                &candidate_generation)) {
            continue;
        }
        *slot = i;
        *generation = candidate_generation;
        return true;
    }
    return false;
}

Status validate_mount_handle(
    State* state,
    MountHandle handle,
    size_t* index) {
    if (!state || !state->initialized) {
        return Status::NotInitialized;
    }
    if (handle.slot == 0 || handle.slot > MAX_MOUNTS ||
        handle.generation == 0) {
        return Status::InvalidHandle;
    }
    const size_t candidate = static_cast<size_t>(handle.slot - 1);
    const Mount& mount_entry = state->mounts[candidate];
    if (!mount_entry.active || mount_entry.generation != handle.generation) {
        return Status::StaleHandle;
    }
    *index = candidate;
    return Status::Ok;
}

bool mount_at_or_below(const State* state, const NormalizedPath& path) {
    for (size_t i = 1; i < MAX_MOUNTS; ++i) {
        const Mount& mount_entry = state->mounts[i];
        if (mount_entry.active &&
            path_is_component_prefix(
                path.value,
                path.length,
                mount_entry.path,
                mount_entry.path_length)) {
            return true;
        }
    }
    return false;
}

bool context_at_or_below(
    const PathContext* context,
    const NormalizedPath& path) {
    size_t root_length = 0;
    size_t cwd_length = 0;
    if (validate_context(context, &root_length, nullptr, &cwd_length) !=
        Status::Ok) {
        return true;
    }
    return path_is_component_prefix(
               path.value, path.length, context->root, root_length) ||
           path_is_component_prefix(
               path.value, path.length, context->cwd, cwd_length);
}

} // namespace

Status initialize(State* state, const FileSystem* root_filesystem) {
    if (!state) {
        return Status::InvalidArgument;
    }
    if (state->initialized) {
        return Status::AlreadyInitialized;
    }
    if (!root_filesystem) {
        return Status::NoRootFilesystem;
    }
    if (!valid_filesystem(*root_filesystem)) {
        return Status::InvalidArgument;
    }

    FileStat root_info = {};
    Status status = backend_status(root_filesystem->operations.stat_path(
        root_filesystem->context, "/", &root_info));
    if (status != Status::Ok) {
        return status;
    }
    if (!valid_backend_stat(root_info)) {
        return Status::BackendFailure;
    }
    if (root_info.type != NodeType::Directory) {
        return Status::NotDirectory;
    }

    reset_state(state);
    Mount& root = state->mounts[0];
    root.active = true;
    root.generation = 1;
    root.path[0] = '/';
    root.path[1] = '\0';
    root.path_length = 1;
    root.filesystem = *root_filesystem;
    state->initialized = true;
    return Status::Ok;
}

Status initialize_path_context(const State* state, PathContext* context) {
    if (!state || !state->initialized) {
        return Status::NotInitialized;
    }
    if (!context) {
        return Status::InvalidArgument;
    }
    clear_bytes(context, sizeof(*context));
    context->root[0] = '/';
    context->cwd[0] = '/';
    return Status::Ok;
}

Status chdir(
    State* state,
    PathContext* context,
    const char* path) {
    if (!state || !state->initialized) {
        return Status::NotInitialized;
    }
    NormalizedPath normalized = {};
    Status status = normalize_path(context, path, &normalized);
    if (status != Status::Ok) {
        return status;
    }
    Vnode vnode = {};
    status = lookup_canonical(state, normalized, &vnode);
    if (status != Status::Ok) {
        return status;
    }
    if (!directory_like(vnode.info.type)) {
        return Status::NotDirectory;
    }

    char candidate[MAX_PATH_LENGTH + 1] = {};
    copy_bytes(candidate, normalized.value, normalized.length + 1);
    copy_bytes(context->cwd, candidate, sizeof(candidate));
    return Status::Ok;
}

Status chroot(
    State* state,
    PathContext* context,
    const char* path) {
    if (!state || !state->initialized) {
        return Status::NotInitialized;
    }
    NormalizedPath normalized = {};
    Status status = normalize_path(context, path, &normalized);
    if (status != Status::Ok) {
        return status;
    }
    Vnode vnode = {};
    status = lookup_canonical(state, normalized, &vnode);
    if (status != Status::Ok) {
        return status;
    }
    if (!directory_like(vnode.info.type)) {
        return Status::NotDirectory;
    }

    PathContext candidate = {};
    copy_bytes(candidate.root, normalized.value, normalized.length + 1);
    copy_bytes(candidate.cwd, normalized.value, normalized.length + 1);
    *context = candidate;
    return Status::Ok;
}

Status getcwd(
    const PathContext* context,
    char* buffer,
    size_t capacity,
    size_t* required_size) {
    if (required_size) {
        *required_size = 0;
    }
    size_t root_length = 0;
    size_t cwd_length = 0;
    Status status = validate_context(
        context, &root_length, nullptr, &cwd_length);
    if (status != Status::Ok) {
        return status;
    }

    const char* visible = nullptr;
    size_t visible_length = 0;
    if (root_length == cwd_length) {
        visible = "/";
        visible_length = 1;
    } else if (root_length == 1) {
        visible = context->cwd;
        visible_length = cwd_length;
    } else {
        visible = context->cwd + root_length;
        visible_length = cwd_length - root_length;
    }
    const size_t required = visible_length + 1;
    if (required_size) {
        *required_size = required;
    }
    if (!buffer || capacity < required) {
        return Status::BufferTooSmall;
    }

    copy_bytes(buffer, visible, required);
    return Status::Ok;
}

Status lookup(
    State* state,
    const PathContext* context,
    const char* path,
    Vnode* vnode) {
    if (!state || !state->initialized) {
        return Status::NotInitialized;
    }
    if (!vnode) {
        return Status::InvalidArgument;
    }
    NormalizedPath normalized = {};
    Status status = normalize_path(context, path, &normalized);
    if (status != Status::Ok) {
        return status;
    }
    Vnode candidate = {};
    status = lookup_canonical(state, normalized, &candidate);
    if (status == Status::Ok) {
        *vnode = candidate;
    }
    return status;
}

Status stat(
    State* state,
    const PathContext* context,
    const char* path,
    FileStat* info) {
    if (!info) {
        return Status::InvalidArgument;
    }
    Vnode vnode = {};
    Status status = lookup(state, context, path, &vnode);
    if (status == Status::Ok) {
        *info = vnode.info;
    }
    return status;
}

Status mount(
    State* state,
    const PathContext* context,
    const char* target,
    const FileSystem* filesystem,
    MountHandle* handle) {
    if (handle) {
        *handle = {};
    }
    if (!state || !state->initialized) {
        return Status::NotInitialized;
    }
    if (!filesystem || !valid_filesystem(*filesystem)) {
        return Status::InvalidArgument;
    }

    size_t slot = 0;
    uint32_t generation = 0;
    if (!find_mount_slot(state, &slot, &generation)) {
        return Status::MountTableFull;
    }

    NormalizedPath normalized = {};
    Status status = normalize_path(context, target, &normalized);
    if (status != Status::Ok) {
        return status;
    }
    if (exact_non_root_mount(state, normalized) ||
        (normalized.length == 1 && normalized.value[0] == '/')) {
        return Status::AlreadyExists;
    }

    Vnode target_vnode = {};
    status = lookup_canonical(state, normalized, &target_vnode);
    if (status != Status::Ok) {
        return status;
    }
    if (!directory_like(target_vnode.info.type)) {
        return Status::NotDirectory;
    }

    FileStat mounted_root = {};
    status = backend_status(filesystem->operations.stat_path(
        filesystem->context, "/", &mounted_root));
    if (status != Status::Ok) {
        return status;
    }
    if (!valid_backend_stat(mounted_root)) {
        return Status::BackendFailure;
    }
    if (mounted_root.type != NodeType::Directory) {
        return Status::NotDirectory;
    }

    Mount candidate = {};
    candidate.active = true;
    candidate.generation = generation;
    candidate.path_length = normalized.length;
    copy_bytes(candidate.path, normalized.value, normalized.length + 1);
    candidate.filesystem = *filesystem;
    state->mounts[slot] = candidate;
    if (handle) {
        handle->slot = static_cast<uint16_t>(slot + 1);
        handle->generation = generation;
    }
    return Status::Ok;
}

Status unmount(State* state, MountHandle handle) {
    size_t index = 0;
    Status status = validate_mount_handle(state, handle, &index);
    if (status != Status::Ok) {
        return status;
    }
    if (index == 0) {
        return Status::RootProtected;
    }

    const Mount& selected = state->mounts[index];
    for (size_t i = 1; i < MAX_MOUNTS; ++i) {
        const Mount& candidate = state->mounts[i];
        if (i != index && candidate.active &&
            path_is_component_prefix(
                selected.path,
                selected.path_length,
                candidate.path,
                candidate.path_length)) {
            return Status::Busy;
        }
    }
    for (size_t i = 0; i < MAX_OPEN_FILES; ++i) {
        const OpenFile& file = state->open_files[i];
        if (file.active && file.mount_slot == index &&
            file.mount_generation == selected.generation) {
            return Status::Busy;
        }
    }

    if (selected.filesystem.operations.sync) {
        status = backend_status(selected.filesystem.operations.sync(
            selected.filesystem.context));
        if (status != Status::Ok) {
            return status;
        }
    }

    const uint32_t retained_generation = selected.generation;
    Mount retired = {};
    retired.generation = retained_generation;
    state->mounts[index] = retired;
    return Status::Ok;
}

namespace {

bool valid_open_flags(OpenFlags flags) {
    const uint32_t raw = static_cast<uint32_t>(flags);
    if ((raw & ~kKnownOpenFlags) != 0) {
        return false;
    }
    const bool readable = has_flag(flags, OpenFlags::Read);
    const bool writable = has_flag(flags, OpenFlags::Write);
    return (readable || writable) &&
           (!has_flag(flags, OpenFlags::Append) || writable);
}

bool find_open_slot(
    const State* state,
    size_t* slot,
    uint32_t* generation) {
    for (size_t i = 0; i < MAX_OPEN_FILES; ++i) {
        if (state->open_files[i].active) {
            continue;
        }
        uint32_t candidate_generation = 0;
        if (!next_generation(
                state->open_files[i].generation,
                &candidate_generation)) {
            continue;
        }
        *slot = i;
        *generation = candidate_generation;
        return true;
    }
    return false;
}

Status validate_open_handle(
    State* state,
    OpenFileHandle handle,
    size_t* index,
    OpenFile** file,
    Mount** mount_entry) {
    if (!state || !state->initialized) {
        return Status::NotInitialized;
    }
    if (handle.slot == 0 || handle.slot > MAX_OPEN_FILES ||
        handle.generation == 0) {
        return Status::InvalidHandle;
    }
    const size_t candidate = static_cast<size_t>(handle.slot - 1);
    OpenFile& selected = state->open_files[candidate];
    if (!selected.active || selected.generation != handle.generation) {
        return Status::StaleHandle;
    }
    if (selected.mount_slot >= MAX_MOUNTS) {
        return Status::BackendFailure;
    }
    Mount& selected_mount = state->mounts[selected.mount_slot];
    if (!selected_mount.active ||
        selected_mount.generation != selected.mount_generation) {
        return Status::StaleHandle;
    }

    if (index) {
        *index = candidate;
    }
    if (file) {
        *file = &selected;
    }
    if (mount_entry) {
        *mount_entry = &selected_mount;
    }
    return Status::Ok;
}

bool request_overflows(uint64_t offset, size_t size) {
    const uint64_t remaining = UINT64_MAX - offset;
    return size > static_cast<size_t>(remaining);
}

Status add_signed_offset(uint64_t base, int64_t delta, uint64_t* result) {
    if (delta >= 0) {
        const uint64_t magnitude = static_cast<uint64_t>(delta);
        if (magnitude > UINT64_MAX - base) {
            return Status::ArithmeticOverflow;
        }
        *result = base + magnitude;
        return Status::Ok;
    }

    const uint64_t magnitude =
        static_cast<uint64_t>(-(delta + 1)) + UINT64_C(1);
    if (magnitude > base) {
        return Status::OutOfRange;
    }
    *result = base - magnitude;
    return Status::Ok;
}

bool valid_directory_entry(const DirectoryEntry& entry) {
    if (entry.name_length == 0 || entry.name_length > MAX_NAME_LENGTH ||
        entry.name[entry.name_length] != '\0' ||
        !valid_backend_stat(entry.info)) {
        return false;
    }
    if (component_equals(entry.name, entry.name_length, ".", 1) ||
        component_equals(entry.name, entry.name_length, "..", 2)) {
        return false;
    }
    for (size_t i = 0; i < entry.name_length; ++i) {
        if (entry.name[i] == '\0' || entry.name[i] == '/') {
            return false;
        }
    }
    return true;
}

bool directory_child_path(
    const char* directory,
    const DirectoryEntry& entry,
    NormalizedPath* child) {
    size_t directory_length = 0;
    if (bounded_length(directory, &directory_length) != Status::Ok) {
        return false;
    }
    const size_t separator = directory_length == 1 ? 0 : 1;
    if (directory_length > MAX_PATH_LENGTH - separator ||
        entry.name_length >
            MAX_PATH_LENGTH - directory_length - separator) {
        return false;
    }

    *child = {};
    copy_bytes(child->value, directory, directory_length);
    child->length = directory_length;
    if (separator != 0) {
        child->value[child->length++] = '/';
    }
    copy_bytes(
        child->value + child->length,
        entry.name,
        entry.name_length);
    child->length += entry.name_length;
    child->value[child->length] = '\0';
    return true;
}

} // namespace

Status open(
    State* state,
    const PathContext* context,
    const char* path,
    OpenFlags flags,
    OpenFileHandle* handle) {
    if (handle) {
        *handle = {};
    }
    if (!state || !state->initialized) {
        return Status::NotInitialized;
    }
    if (!handle) {
        return Status::InvalidArgument;
    }
    if (!valid_open_flags(flags)) {
        return Status::InvalidFlags;
    }

    size_t slot = 0;
    uint32_t generation = 0;
    if (!find_open_slot(state, &slot, &generation)) {
        return Status::OpenFileTableFull;
    }

    NormalizedPath normalized = {};
    Status status = normalize_path(context, path, &normalized);
    if (status != Status::Ok) {
        return status;
    }
    Route route = {};
    status = route_path(state, normalized, &route);
    if (status != Status::Ok) {
        return status;
    }
    Mount& mount_entry = state->mounts[route.mount_index];
    if (!mount_entry.filesystem.operations.open) {
        return Status::Unsupported;
    }
    if (has_flag(flags, OpenFlags::Write) &&
        mount_entry.filesystem.read_only) {
        return Status::ReadOnly;
    }

    Vnode vnode = {};
    status = lookup_canonical(state, normalized, &vnode);
    if (status != Status::Ok) {
        return status;
    }
    const bool requested_directory = has_flag(flags, OpenFlags::Directory);
    const bool is_directory = directory_like(vnode.info.type);
    if (requested_directory && !is_directory) {
        return Status::NotDirectory;
    }
    if (!requested_directory && is_directory) {
        return Status::IsDirectory;
    }

    BackendFile backend_file = {};
    status = backend_status(mount_entry.filesystem.operations.open(
        mount_entry.filesystem.context,
        route.backend_path,
        flags,
        &backend_file));
    if (status != Status::Ok) {
        return status;
    }

    OpenFile candidate = {};
    candidate.active = true;
    candidate.generation = generation;
    candidate.reference_count = 1;
    candidate.flags = flags;
    candidate.offset = has_flag(flags, OpenFlags::Append)
        ? vnode.info.size
        : UINT64_C(0);
    candidate.mount_slot = static_cast<uint16_t>(route.mount_index);
    candidate.mount_generation = mount_entry.generation;
    candidate.type = vnode.info.type;
    candidate.node_flags = vnode.info.flags;
    candidate.backend_file = backend_file;
    copy_bytes(
        candidate.global_path,
        normalized.value,
        normalized.length + 1);
    state->open_files[slot] = candidate;

    handle->slot = static_cast<uint16_t>(slot + 1);
    handle->generation = generation;
    return Status::Ok;
}

Status retain(State* state, OpenFileHandle handle) {
    OpenFile* file = nullptr;
    Status status = validate_open_handle(
        state, handle, nullptr, &file, nullptr);
    if (status != Status::Ok) {
        return status;
    }
    if (file->reference_count == UINT32_MAX) {
        return Status::ArithmeticOverflow;
    }
    ++file->reference_count;
    return Status::Ok;
}

Status close(State* state, OpenFileHandle handle) {
    size_t index = 0;
    OpenFile* file = nullptr;
    Mount* mount_entry = nullptr;
    Status status = validate_open_handle(
        state, handle, &index, &file, &mount_entry);
    if (status != Status::Ok) {
        return status;
    }
    if (file->reference_count == 0) {
        return Status::BackendFailure;
    }
    if (file->reference_count > 1) {
        --file->reference_count;
        return Status::Ok;
    }

    mount_entry->filesystem.operations.close(
        mount_entry->filesystem.context,
        &file->backend_file);
    const uint32_t retained_generation = file->generation;
    OpenFile retired = {};
    retired.generation = retained_generation;
    state->open_files[index] = retired;
    return Status::Ok;
}

Status read(
    State* state,
    OpenFileHandle handle,
    void* buffer,
    size_t size,
    size_t* bytes_read) {
    if (bytes_read) {
        *bytes_read = 0;
    }
    OpenFile* file = nullptr;
    Mount* mount_entry = nullptr;
    Status status = validate_open_handle(
        state, handle, nullptr, &file, &mount_entry);
    if (status != Status::Ok) {
        return status;
    }
    if (!has_flag(file->flags, OpenFlags::Read)) {
        return Status::PermissionDenied;
    }
    if (directory_like(file->type)) {
        return Status::IsDirectory;
    }
    if (!buffer && size != 0) {
        return Status::InvalidArgument;
    }
    if (size == 0) {
        return Status::Ok;
    }
    if (!mount_entry->filesystem.operations.read) {
        return Status::Unsupported;
    }
    if (request_overflows(file->offset, size)) {
        return Status::ArithmeticOverflow;
    }

    size_t completed = 0;
    status = backend_status(mount_entry->filesystem.operations.read(
        mount_entry->filesystem.context,
        &file->backend_file,
        file->offset,
        buffer,
        size,
        &completed));
    if (status != Status::Ok) {
        return status;
    }
    if (completed > size) {
        return Status::BackendFailure;
    }
    file->offset += static_cast<uint64_t>(completed);
    if (bytes_read) {
        *bytes_read = completed;
    }
    return Status::Ok;
}

Status write(
    State* state,
    OpenFileHandle handle,
    const void* buffer,
    size_t size,
    size_t* bytes_written) {
    if (bytes_written) {
        *bytes_written = 0;
    }
    OpenFile* file = nullptr;
    Mount* mount_entry = nullptr;
    Status status = validate_open_handle(
        state, handle, nullptr, &file, &mount_entry);
    if (status != Status::Ok) {
        return status;
    }
    if (!has_flag(file->flags, OpenFlags::Write)) {
        return Status::PermissionDenied;
    }
    if (directory_like(file->type)) {
        return Status::IsDirectory;
    }
    if (mount_entry->filesystem.read_only) {
        return Status::ReadOnly;
    }
    if (!buffer && size != 0) {
        return Status::InvalidArgument;
    }
    if (size == 0) {
        return Status::Ok;
    }
    if (!mount_entry->filesystem.operations.write) {
        return Status::Unsupported;
    }

    uint64_t write_offset = file->offset;
    if (has_flag(file->flags, OpenFlags::Append)) {
        if (!mount_entry->filesystem.operations.stat_open) {
            return Status::Unsupported;
        }
        FileStat current = {};
        status = backend_status(mount_entry->filesystem.operations.stat_open(
            mount_entry->filesystem.context,
            &file->backend_file,
            &current));
        if (status != Status::Ok) {
            return status;
        }
        if (!valid_backend_stat(current)) {
            return Status::BackendFailure;
        }
        write_offset = current.size;
    }
    if (request_overflows(write_offset, size)) {
        return Status::ArithmeticOverflow;
    }

    size_t completed = 0;
    status = backend_status(mount_entry->filesystem.operations.write(
        mount_entry->filesystem.context,
        &file->backend_file,
        write_offset,
        buffer,
        size,
        &completed));
    if (status != Status::Ok) {
        return status;
    }
    if (completed > size) {
        return Status::BackendFailure;
    }
    file->offset = write_offset + static_cast<uint64_t>(completed);
    if (bytes_written) {
        *bytes_written = completed;
    }
    return Status::Ok;
}

Status seek(
    State* state,
    OpenFileHandle handle,
    int64_t offset,
    SeekOrigin origin,
    uint64_t* new_offset) {
    if (new_offset) {
        *new_offset = 0;
    }
    OpenFile* file = nullptr;
    Mount* mount_entry = nullptr;
    Status status = validate_open_handle(
        state, handle, nullptr, &file, &mount_entry);
    if (status != Status::Ok) {
        return status;
    }
    if (!has_flag(file->node_flags, NodeFlags::Seekable)) {
        return Status::NotSeekable;
    }

    uint64_t base = 0;
    switch (origin) {
        case SeekOrigin::Begin:
            base = 0;
            break;
        case SeekOrigin::Current:
            base = file->offset;
            break;
        case SeekOrigin::End: {
            if (!mount_entry->filesystem.operations.stat_open) {
                return Status::Unsupported;
            }
            FileStat current = {};
            status = backend_status(
                mount_entry->filesystem.operations.stat_open(
                    mount_entry->filesystem.context,
                    &file->backend_file,
                    &current));
            if (status != Status::Ok) {
                return status;
            }
            if (!valid_backend_stat(current)) {
                return Status::BackendFailure;
            }
            base = current.size;
            break;
        }
        default:
            return Status::InvalidArgument;
    }

    uint64_t candidate = 0;
    status = add_signed_offset(base, offset, &candidate);
    if (status != Status::Ok) {
        return status;
    }
    file->offset = candidate;
    if (new_offset) {
        *new_offset = candidate;
    }
    return Status::Ok;
}

Status readdir(
    State* state,
    OpenFileHandle handle,
    DirectoryEntry* entry) {
    if (!entry) {
        return Status::InvalidArgument;
    }
    OpenFile* file = nullptr;
    Mount* mount_entry = nullptr;
    Status status = validate_open_handle(
        state, handle, nullptr, &file, &mount_entry);
    if (status != Status::Ok) {
        return status;
    }
    if (!directory_like(file->type) ||
        !has_flag(file->flags, OpenFlags::Directory)) {
        return Status::NotDirectory;
    }
    if (!has_flag(file->flags, OpenFlags::Read)) {
        return Status::PermissionDenied;
    }
    if (!mount_entry->filesystem.operations.readdir) {
        return Status::Unsupported;
    }

    DirectoryEntry candidate = {};
    uint64_t next_cookie = file->offset;
    status = backend_status(mount_entry->filesystem.operations.readdir(
        mount_entry->filesystem.context,
        &file->backend_file,
        file->offset,
        &candidate,
        &next_cookie));
    if (status != Status::Ok) {
        return status;
    }
    if (!valid_directory_entry(candidate) || next_cookie == file->offset) {
        return Status::BackendFailure;
    }

    NormalizedPath child = {};
    if (!directory_child_path(file->global_path, candidate, &child)) {
        return Status::BackendFailure;
    }
    if (exact_non_root_mount(state, child)) {
        candidate.info.type = NodeType::MountPoint;
    }

    file->offset = next_cookie;
    *entry = candidate;
    return Status::Ok;
}

namespace {

bool is_context_root(
    const PathContext* context,
    const NormalizedPath& path) {
    size_t root_length = 0;
    return validate_context(context, &root_length, nullptr, nullptr) ==
               Status::Ok &&
           path_equals(
               context->root,
               root_length,
               path.value,
               path.length);
}

Status prepare_mutation(
    State* state,
    const PathContext* context,
    const char* path,
    bool allow_trailing_separator,
    NormalizedPath* normalized,
    Route* route) {
    if (!state || !state->initialized) {
        return Status::NotInitialized;
    }
    Status status = normalize_path(context, path, normalized);
    if (status != Status::Ok) {
        return status;
    }
    if (normalized->terminal_special ||
        (!allow_trailing_separator && normalized->trailing_separator)) {
        return Status::InvalidPath;
    }
    status = route_path(state, *normalized, route);
    if (status != Status::Ok) {
        return status;
    }
    if (state->mounts[route->mount_index].filesystem.read_only) {
        return Status::ReadOnly;
    }
    return Status::Ok;
}

Status create_like(
    State* state,
    const PathContext* context,
    const char* path,
    bool directory) {
    NormalizedPath normalized = {};
    Route route = {};
    Status status = prepare_mutation(
        state, context, path, directory, &normalized, &route);
    if (status != Status::Ok) {
        return status;
    }
    if (exact_non_root_mount(state, normalized)) {
        return Status::AlreadyExists;
    }

    Mount& mount_entry = state->mounts[route.mount_index];
    Status (*operation)(void*, const char*) = directory
        ? mount_entry.filesystem.operations.mkdir
        : mount_entry.filesystem.operations.create;
    if (!operation) {
        return Status::Unsupported;
    }
    return backend_status(operation(
        mount_entry.filesystem.context,
        route.backend_path));
}

Status remove_like(
    State* state,
    const PathContext* context,
    const char* path,
    bool directory) {
    NormalizedPath normalized = {};
    Route route = {};
    Status status = prepare_mutation(
        state, context, path, directory, &normalized, &route);
    if (status != Status::Ok) {
        return status;
    }
    if (is_context_root(context, normalized) ||
        (normalized.length == 1 && normalized.value[0] == '/')) {
        return Status::RootProtected;
    }
    if (mount_at_or_below(state, normalized) ||
        context_at_or_below(context, normalized)) {
        return Status::Busy;
    }

    Mount& mount_entry = state->mounts[route.mount_index];
    Status (*operation)(void*, const char*) = directory
        ? mount_entry.filesystem.operations.rmdir
        : mount_entry.filesystem.operations.unlink;
    if (!operation) {
        return Status::Unsupported;
    }
    return backend_status(operation(
        mount_entry.filesystem.context,
        route.backend_path));
}

} // namespace

Status create(
    State* state,
    const PathContext* context,
    const char* path) {
    return create_like(state, context, path, false);
}

Status unlink(
    State* state,
    const PathContext* context,
    const char* path) {
    return remove_like(state, context, path, false);
}

Status rename(
    State* state,
    const PathContext* context,
    const char* source_path,
    const char* destination_path) {
    if (!state || !state->initialized) {
        return Status::NotInitialized;
    }

    NormalizedPath source = {};
    Status status = normalize_path(context, source_path, &source);
    if (status != Status::Ok) {
        return status;
    }
    NormalizedPath destination = {};
    status = normalize_path(context, destination_path, &destination);
    if (status != Status::Ok) {
        return status;
    }
    if (source.terminal_special || destination.terminal_special ||
        destination.trailing_separator) {
        return Status::InvalidPath;
    }
    if (is_context_root(context, source) ||
        (source.length == 1 && source.value[0] == '/')) {
        return Status::RootProtected;
    }
    if (mount_at_or_below(state, source) ||
        mount_at_or_below(state, destination) ||
        context_at_or_below(context, source)) {
        return Status::Busy;
    }
    if (path_equals(
            source.value,
            source.length,
            destination.value,
            destination.length)) {
        return Status::Ok;
    }

    Route source_route = {};
    status = route_path(state, source, &source_route);
    if (status != Status::Ok) {
        return status;
    }
    Route destination_route = {};
    status = route_path(state, destination, &destination_route);
    if (status != Status::Ok) {
        return status;
    }
    const Mount& source_mount = state->mounts[source_route.mount_index];
    const Mount& destination_mount =
        state->mounts[destination_route.mount_index];
    if (source_route.mount_index != destination_route.mount_index ||
        source_mount.generation != destination_mount.generation) {
        return Status::CrossDevice;
    }
    if (source_mount.filesystem.read_only) {
        return Status::ReadOnly;
    }
    if (!source_mount.filesystem.operations.rename) {
        return Status::Unsupported;
    }

    return backend_status(source_mount.filesystem.operations.rename(
        source_mount.filesystem.context,
        source_route.backend_path,
        destination_route.backend_path));
}

Status mkdir(
    State* state,
    const PathContext* context,
    const char* path) {
    return create_like(state, context, path, true);
}

Status rmdir(
    State* state,
    const PathContext* context,
    const char* path) {
    return remove_like(state, context, path, true);
}

Status sync_all(State* state) {
    if (!state || !state->initialized) {
        return Status::NotInitialized;
    }

    Status first_failure = Status::Ok;
    for (size_t i = 0; i < MAX_MOUNTS; ++i) {
        Mount& mount_entry = state->mounts[i];
        if (!mount_entry.active) {
            continue;
        }
        if (!mount_entry.filesystem.operations.sync) {
            if (first_failure == Status::Ok) {
                first_failure = Status::Unsupported;
            }
            continue;
        }
        const Status status = backend_status(
            mount_entry.filesystem.operations.sync(
                mount_entry.filesystem.context));
        if (status != Status::Ok && first_failure == Status::Ok) {
            first_failure = status;
        }
    }
    return first_failure;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotInitialized: return "not initialized";
        case Status::AlreadyInitialized: return "already initialized";
        case Status::InvalidArgument: return "invalid argument";
        case Status::InvalidFlags: return "invalid flags";
        case Status::InvalidPath: return "invalid path";
        case Status::PathTooLong: return "path too long";
        case Status::NameTooLong: return "name too long";
        case Status::PathTooDeep: return "path too deep";
        case Status::NoRootFilesystem: return "no root filesystem";
        case Status::NotFound: return "not found";
        case Status::AlreadyExists: return "already exists";
        case Status::NotDirectory: return "not a directory";
        case Status::IsDirectory: return "is a directory";
        case Status::DirectoryNotEmpty: return "directory not empty";
        case Status::RootProtected: return "root is protected";
        case Status::ReadOnly: return "read-only filesystem";
        case Status::PermissionDenied: return "permission denied";
        case Status::Unsupported: return "operation unsupported";
        case Status::NotSeekable: return "file is not seekable";
        case Status::EndOfDirectory: return "end of directory";
        case Status::BufferTooSmall: return "buffer too small";
        case Status::InvalidHandle: return "invalid handle";
        case Status::StaleHandle: return "stale handle";
        case Status::MountNotFound: return "mount not found";
        case Status::Busy: return "resource busy";
        case Status::MountTableFull: return "mount table full";
        case Status::OpenFileTableFull: return "open-file table full";
        case Status::CrossDevice: return "cross-device operation";
        case Status::OutOfRange: return "out of range";
        case Status::ArithmeticOverflow: return "arithmetic overflow";
        case Status::NoSpace: return "no space left";
        case Status::OutOfMemory: return "out of memory";
        case Status::IoError: return "I/O error";
        case Status::CorruptFilesystem: return "corrupt filesystem";
        case Status::BackendFailure: return "invalid backend result";
    }
    return "unknown status";
}

} // namespace fs::vfs
