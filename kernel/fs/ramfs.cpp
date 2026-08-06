#include "ramfs.hpp"
#include "../memory/allocator.hpp"

namespace fs {

namespace {

static constexpr size_t RAMFS_MAX_PATH_COMPONENTS = (RAMFS_MAX_PATH_LENGTH + 1) / 2;

struct PathComponent {
    const char* begin;
    size_t length;
};

struct ParsedPath {
    PathComponent components[RAMFS_MAX_PATH_COMPONENTS];
    size_t count;
    bool trailing_separator;
};

static FileEntry* g_root = nullptr;
static size_t g_node_count = 0;
static size_t g_total_file_bytes = 0;
static Status g_last_status = Status::NotInitialized;
static char g_empty_content[1] = {'\0'};

Status finish(Status status) {
    g_last_status = status;
    return status;
}

void clear_bytes(void* destination, size_t size) {
    unsigned char* bytes = static_cast<unsigned char*>(destination);
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = 0;
    }
}

void copy_bytes(void* destination, const void* source, size_t size) {
    unsigned char* output = static_cast<unsigned char*>(destination);
    const unsigned char* input = static_cast<const unsigned char*>(source);
    for (size_t i = 0; i < size; ++i) {
        output[i] = input[i];
    }
}

Status bounded_length(const char* text, size_t maximum, size_t* out_length, Status too_long) {
    if (!text || !out_length) {
        return Status::InvalidArgument;
    }

    for (size_t i = 0; i <= maximum; ++i) {
        if (text[i] == '\0') {
            *out_length = i;
            return Status::Ok;
        }
    }
    return too_long;
}

bool component_equals(const PathComponent& component, const char* literal, size_t length) {
    if (component.length != length) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        if (component.begin[i] != literal[i]) {
            return false;
        }
    }
    return true;
}

Status parse_path(const char* path, ParsedPath* parsed) {
    if (!path || !parsed) {
        return Status::InvalidArgument;
    }

    size_t path_length = 0;
    Status length_status =
        bounded_length(path, RAMFS_MAX_PATH_LENGTH, &path_length, Status::PathTooLong);
    if (length_status != Status::Ok) {
        return length_status;
    }
    if (path_length == 0) {
        return Status::InvalidPath;
    }

    parsed->count = 0;
    parsed->trailing_separator = path[path_length - 1] == '/';

    size_t position = 0;
    size_t logical_depth = 0;
    while (position < path_length) {
        while (position < path_length && path[position] == '/') {
            ++position;
        }
        if (position == path_length) {
            break;
        }

        const size_t component_start = position;
        while (position < path_length && path[position] != '/') {
            ++position;
        }

        PathComponent component = {
            path + component_start,
            position - component_start
        };

        if (component.length > RAMFS_MAX_NAME_LENGTH) {
            return Status::NameTooLong;
        }
        if (component_equals(component, "..", 2)) {
            if (logical_depth == 0) {
                return Status::InvalidPath;
            }
            --logical_depth;
        } else if (!component_equals(component, ".", 1)) {
            if (logical_depth == RAMFS_MAX_PATH_DEPTH) {
                return Status::PathTooDeep;
            }
            ++logical_depth;
        }
        if (parsed->count == RAMFS_MAX_PATH_COMPONENTS) {
            return Status::PathTooDeep;
        }

        parsed->components[parsed->count++] = component;
    }

    return Status::Ok;
}

bool entry_name_equals(const FileEntry* entry, const PathComponent& component) {
    if (!entry || entry->name_length != component.length) {
        return false;
    }
    for (size_t i = 0; i < component.length; ++i) {
        if (entry->name[i] != component.begin[i]) {
            return false;
        }
    }
    return true;
}

FileEntry* find_child(FileEntry* parent, const PathComponent& component) {
    if (!parent || !parent->is_directory) {
        return nullptr;
    }

    for (size_t i = 0; i < parent->child_count; ++i) {
        FileEntry* child = parent->children[i];
        if (entry_name_equals(child, component)) {
            return child;
        }
    }
    return nullptr;
}

Status resolve_components(
    const ParsedPath& parsed,
    size_t component_count,
    FileEntry** out_entry
) {
    if (!g_root) {
        return Status::NotInitialized;
    }
    if (!out_entry || component_count > parsed.count) {
        return Status::InvalidArgument;
    }

    FileEntry* current = g_root;
    for (size_t i = 0; i < component_count; ++i) {
        if (!current->is_directory) {
            return Status::NotDirectory;
        }

        if (component_equals(parsed.components[i], ".", 1)) {
            continue;
        }
        if (component_equals(parsed.components[i], "..", 2)) {
            if (current == g_root || !current->parent) {
                return Status::InvalidPath;
            }
            current = current->parent;
            continue;
        }

        FileEntry* child = find_child(current, parsed.components[i]);
        if (!child) {
            return Status::NotFound;
        }
        current = child;
    }

    *out_entry = current;
    return Status::Ok;
}

Status resolve_path(const char* path, ParsedPath* parsed, FileEntry** out_entry) {
    Status status = parse_path(path, parsed);
    if (status != Status::Ok) {
        return status;
    }

    status = resolve_components(*parsed, parsed->count, out_entry);
    if (status != Status::Ok) {
        return status;
    }
    if (parsed->trailing_separator && !(*out_entry)->is_directory) {
        return Status::NotDirectory;
    }
    return Status::Ok;
}

FileEntry* allocate_entry(const PathComponent& name, bool is_directory) {
    if (g_node_count >= RAMFS_MAX_NODES) {
        return nullptr;
    }

    FileEntry* entry =
        static_cast<FileEntry*>(memory::kmalloc(sizeof(FileEntry), alignof(FileEntry)));
    if (!entry) {
        return nullptr;
    }

    clear_bytes(entry, sizeof(FileEntry));
    for (size_t i = 0; i < name.length; ++i) {
        entry->name[i] = name.begin[i];
    }
    entry->name[name.length] = '\0';
    entry->name_length = name.length;
    entry->is_directory = is_directory;

    if (!is_directory) {
        entry->content = static_cast<char*>(memory::kmalloc(1, alignof(char)));
        if (!entry->content) {
            memory::kfree(entry);
            return nullptr;
        }
        entry->content[0] = '\0';
        entry->capacity = 1;
    }

    ++g_node_count;
    return entry;
}

void release_entry_storage(FileEntry* entry) {
    if (!entry) {
        return;
    }

    if (entry->content) {
        memory::kfree(entry->content);
        entry->content = nullptr;
    }
    if (entry->children) {
        memory::kfree(entry->children);
        entry->children = nullptr;
    }
    memory::kfree(entry);
}

Status ensure_child_capacity(FileEntry* directory) {
    if (!directory || !directory->is_directory) {
        return Status::NotDirectory;
    }
    if (directory->child_count >= RAMFS_MAX_CHILDREN) {
        return Status::TooManyChildren;
    }
    if (directory->child_count < directory->child_capacity) {
        return Status::Ok;
    }

    size_t new_capacity = directory->child_capacity == 0 ? 4 : directory->child_capacity * 2;
    if (new_capacity > RAMFS_MAX_CHILDREN) {
        new_capacity = RAMFS_MAX_CHILDREN;
    }

    FileEntry** new_children = static_cast<FileEntry**>(
        memory::kmalloc(sizeof(FileEntry*) * new_capacity, alignof(FileEntry*))
    );
    if (!new_children) {
        return Status::OutOfMemory;
    }

    for (size_t i = 0; i < directory->child_count; ++i) {
        new_children[i] = directory->children[i];
    }
    for (size_t i = directory->child_count; i < new_capacity; ++i) {
        new_children[i] = nullptr;
    }

    if (directory->children) {
        memory::kfree(directory->children);
    }
    directory->children = new_children;
    directory->child_capacity = new_capacity;
    return Status::Ok;
}

Status attach_child(FileEntry* parent, FileEntry* child) {
    Status status = ensure_child_capacity(parent);
    if (status != Status::Ok) {
        return status;
    }

    parent->children[parent->child_count++] = child;
    child->parent = parent;
    return Status::Ok;
}

void detach_child(FileEntry* parent, FileEntry* child) {
    if (!parent || !child) {
        return;
    }

    for (size_t i = 0; i < parent->child_count; ++i) {
        if (parent->children[i] != child) {
            continue;
        }
        for (size_t j = i + 1; j < parent->child_count; ++j) {
            parent->children[j - 1] = parent->children[j];
        }
        --parent->child_count;
        parent->children[parent->child_count] = nullptr;
        child->parent = nullptr;
        return;
    }
}

void destroy_subtree(FileEntry* entry) {
    if (!entry) {
        return;
    }

    while (entry->child_count > 0) {
        FileEntry* child = entry->children[entry->child_count - 1];
        --entry->child_count;
        destroy_subtree(child);
    }

    if (!entry->is_directory) {
        if (g_total_file_bytes >= entry->size) {
            g_total_file_bytes -= entry->size;
        } else {
            g_total_file_bytes = 0;
        }
    }
    if (g_node_count > 0) {
        --g_node_count;
    }
    release_entry_storage(entry);
}

Status create_entry(const char* path, bool is_directory, FileEntry** out_entry) {
    if (out_entry) {
        *out_entry = nullptr;
    }
    if (!g_root) {
        return Status::NotInitialized;
    }

    ParsedPath parsed = {};
    Status status = parse_path(path, &parsed);
    if (status != Status::Ok) {
        return status;
    }
    if (parsed.count == 0) {
        return Status::RootProtected;
    }
    const PathComponent& final_component = parsed.components[parsed.count - 1];
    if (component_equals(final_component, ".", 1) ||
        component_equals(final_component, "..", 2)) {
        return Status::InvalidPath;
    }
    if (parsed.trailing_separator && !is_directory) {
        return Status::InvalidPath;
    }
    if (g_node_count >= RAMFS_MAX_NODES) {
        return Status::TooManyNodes;
    }

    FileEntry* parent = nullptr;
    status = resolve_components(parsed, parsed.count - 1, &parent);
    if (status != Status::Ok) {
        return status;
    }
    if (!parent->is_directory) {
        return Status::NotDirectory;
    }
    if (find_child(parent, final_component)) {
        return Status::AlreadyExists;
    }
    if (parent->child_count >= RAMFS_MAX_CHILDREN) {
        return Status::TooManyChildren;
    }

    FileEntry* entry = allocate_entry(final_component, is_directory);
    if (!entry) {
        return g_node_count >= RAMFS_MAX_NODES
            ? Status::TooManyNodes
            : Status::OutOfMemory;
    }

    status = attach_child(parent, entry);
    if (status != Status::Ok) {
        if (g_node_count > 0) {
            --g_node_count;
        }
        release_entry_storage(entry);
        return status;
    }

    if (out_entry) {
        *out_entry = entry;
    }
    return Status::Ok;
}

Status write_entry(FileEntry* entry, const void* data, size_t size) {
    if (!entry) {
        return Status::InvalidArgument;
    }
    if (entry->is_directory) {
        return Status::IsDirectory;
    }
    if (!data && size != 0) {
        return Status::InvalidArgument;
    }
    if (size > RAMFS_MAX_FILE_SIZE) {
        return Status::FileTooLarge;
    }
    if (entry->size > g_total_file_bytes) {
        return Status::StorageLimitReached;
    }
    const size_t other_file_bytes = g_total_file_bytes - entry->size;
    if (size > RAMFS_MAX_TOTAL_FILE_BYTES - other_file_bytes) {
        return Status::StorageLimitReached;
    }

    const size_t required_capacity = size + 1;
    const bool should_resize =
        required_capacity > entry->capacity ||
        (required_capacity < entry->capacity &&
         required_capacity <= entry->capacity / 2);

    if (should_resize) {
        char* new_content =
            static_cast<char*>(memory::kmalloc(required_capacity, alignof(char)));
        if (!new_content) {
            return Status::OutOfMemory;
        }
        if (size != 0) {
            copy_bytes(new_content, data, size);
        }
        new_content[size] = '\0';

        char* old_content = entry->content;
        entry->content = new_content;
        entry->capacity = required_capacity;
        if (old_content) {
            memory::kfree(old_content);
        }
    } else {
        if (size != 0) {
            copy_bytes(entry->content, data, size);
        }
        entry->content[size] = '\0';
    }

    g_total_file_bytes = g_total_file_bytes - entry->size + size;
    entry->size = size;
    return Status::Ok;
}

Status resolve_destination(
    const char* path,
    bool source_is_directory,
    ParsedPath* parsed,
    FileEntry** parent,
    const PathComponent** name,
    FileEntry** existing
) {
    if (!parsed || !parent || !name || !existing) {
        return Status::InvalidArgument;
    }

    Status status = parse_path(path, parsed);
    if (status != Status::Ok) {
        return status;
    }
    if (parsed->count == 0) {
        return Status::RootProtected;
    }

    const PathComponent& final_component =
        parsed->components[parsed->count - 1];
    if (component_equals(final_component, ".", 1) ||
        component_equals(final_component, "..", 2) ||
        (parsed->trailing_separator && !source_is_directory)) {
        return Status::InvalidPath;
    }

    status = resolve_components(*parsed, parsed->count - 1, parent);
    if (status != Status::Ok) {
        return status;
    }
    if (!(*parent)->is_directory) {
        return Status::NotDirectory;
    }

    *name = &final_component;
    *existing = find_child(*parent, final_component);
    return Status::Ok;
}

bool is_same_or_descendant(FileEntry* possible_descendant, FileEntry* ancestor) {
    for (FileEntry* current = possible_descendant;
         current;
         current = current->parent) {
        if (current == ancestor) {
            return true;
        }
    }
    return false;
}

size_t entry_depth(const FileEntry* entry) {
    size_t depth = 0;
    for (const FileEntry* current = entry;
         current && current != g_root;
         current = current->parent) {
        ++depth;
    }
    return depth;
}

size_t entry_path_length(const FileEntry* entry) {
    if (!entry || entry == g_root) {
        return 1;
    }

    size_t length = 0;
    for (const FileEntry* current = entry;
         current && current != g_root;
         current = current->parent) {
        length += current->name_length + 1;
    }
    return length;
}

Status subtree_fits_at(
    const FileEntry* entry,
    size_t depth,
    size_t path_length
) {
    if (!entry) {
        return Status::InvalidArgument;
    }
    if (depth > RAMFS_MAX_PATH_DEPTH) {
        return Status::PathTooDeep;
    }
    if (path_length > RAMFS_MAX_PATH_LENGTH) {
        return Status::PathTooLong;
    }

    for (size_t i = 0; i < entry->child_count; ++i) {
        const FileEntry* child = entry->children[i];
        if (!child) {
            return Status::InvalidPath;
        }
        if (path_length > RAMFS_MAX_PATH_LENGTH - 1 ||
            child->name_length > RAMFS_MAX_PATH_LENGTH - path_length - 1) {
            return Status::PathTooLong;
        }
        const Status status = subtree_fits_at(
            child,
            depth + 1,
            path_length + 1 + child->name_length);
        if (status != Status::Ok) {
            return status;
        }
    }
    return Status::Ok;
}

Status subtree_fits_destination(
    const FileEntry* entry,
    const FileEntry* destination_parent,
    size_t destination_name_length
) {
    const size_t parent_depth = entry_depth(destination_parent);
    const size_t parent_length = entry_path_length(destination_parent);
    if (parent_depth >= RAMFS_MAX_PATH_DEPTH) {
        return Status::PathTooDeep;
    }

    const size_t separator = destination_parent == g_root ? 0 : 1;
    if (parent_length > RAMFS_MAX_PATH_LENGTH - separator ||
        destination_name_length >
            RAMFS_MAX_PATH_LENGTH - parent_length - separator) {
        return Status::PathTooLong;
    }
    return subtree_fits_at(
        entry,
        parent_depth + 1,
        parent_length + separator + destination_name_length);
}

void set_entry_name(FileEntry* entry, const PathComponent& name) {
    for (size_t i = 0; i < name.length; ++i) {
        entry->name[i] = name.begin[i];
    }
    entry->name[name.length] = '\0';
    entry->name_length = name.length;
}

} // namespace

Status initialize_ramfs() {
    if (g_root) {
        return finish(Status::Ok);
    }

    const char root_name[] = "/";
    const PathComponent root_component = {root_name, 1};
    g_root = allocate_entry(root_component, true);
    if (!g_root) {
        return finish(
            g_node_count >= RAMFS_MAX_NODES ? Status::TooManyNodes : Status::OutOfMemory
        );
    }
    g_root->parent = nullptr;
    return finish(Status::Ok);
}

Status create_file_at(const char* path, FileEntry** out_entry) {
    return finish(create_entry(path, false, out_entry));
}

Status create_directory_at(const char* path, FileEntry** out_entry) {
    return finish(create_entry(path, true, out_entry));
}

Status read_file_data(
    const char* path,
    void* buffer,
    size_t capacity,
    size_t* bytes_read
) {
    if (bytes_read) {
        *bytes_read = 0;
    }

    ParsedPath parsed = {};
    FileEntry* entry = nullptr;
    Status status = resolve_path(path, &parsed, &entry);
    if (status != Status::Ok) {
        return finish(status);
    }
    if (entry->is_directory) {
        return finish(Status::IsDirectory);
    }
    if (bytes_read) {
        *bytes_read = entry->size;
    }
    if (capacity < entry->size || (!buffer && entry->size != 0)) {
        return finish(Status::BufferTooSmall);
    }
    if (entry->size != 0) {
        copy_bytes(buffer, entry->content, entry->size);
    }
    return finish(Status::Ok);
}

Status write_file_data(
    const char* path,
    const void* data,
    size_t size,
    bool create_if_missing
) {
    if (!g_root) {
        return finish(Status::NotInitialized);
    }
    if (!data && size != 0) {
        return finish(Status::InvalidArgument);
    }
    if (size > RAMFS_MAX_FILE_SIZE) {
        return finish(Status::FileTooLarge);
    }

    ParsedPath parsed = {};
    FileEntry* entry = nullptr;
    Status status = resolve_path(path, &parsed, &entry);
    bool created = false;
    if (status == Status::NotFound && create_if_missing) {
        status = create_entry(path, false, &entry);
        created = status == Status::Ok;
    }
    if (status != Status::Ok) {
        return finish(status);
    }

    status = write_entry(entry, data, size);
    if (status != Status::Ok && created) {
        detach_child(entry->parent, entry);
        destroy_subtree(entry);
    }
    return finish(status);
}

Status stat_path(const char* path, FileStat* out_stat) {
    if (!out_stat) {
        return finish(Status::InvalidArgument);
    }

    ParsedPath parsed = {};
    FileEntry* entry = nullptr;
    Status status = resolve_path(path, &parsed, &entry);
    if (status != Status::Ok) {
        return finish(status);
    }

    out_stat->type = entry->is_directory ? EntryType::Directory : EntryType::File;
    out_stat->size = entry->size;
    out_stat->child_count = entry->child_count;
    return finish(Status::Ok);
}

Status list_directory(const char* path, ListCallback callback, void* context) {
    if (!callback) {
        return finish(Status::InvalidArgument);
    }

    ParsedPath parsed = {};
    FileEntry* directory = nullptr;
    Status status = resolve_path(path, &parsed, &directory);
    if (status != Status::Ok) {
        return finish(status);
    }
    if (!directory->is_directory) {
        return finish(Status::NotDirectory);
    }

    for (size_t i = 0; i < directory->child_count; ++i) {
        FileEntry* child = directory->children[i];
        FileStat info = {
            child->is_directory ? EntryType::Directory : EntryType::File,
            child->size,
            child->child_count
        };
        if (!callback(child->name, &info, context)) {
            return finish(Status::IterationStopped);
        }
    }
    return finish(Status::Ok);
}

Status remove_path(const char* path, bool recursive) {
    ParsedPath parsed = {};
    FileEntry* entry = nullptr;
    Status status = resolve_path(path, &parsed, &entry);
    if (status != Status::Ok) {
        return finish(status);
    }
    if (entry == g_root) {
        return finish(Status::RootProtected);
    }
    if (entry->is_directory && entry->child_count != 0 && !recursive) {
        return finish(Status::DirectoryNotEmpty);
    }

    FileEntry* parent = entry->parent;
    detach_child(parent, entry);
    destroy_subtree(entry);
    return finish(Status::Ok);
}

Status copy_file(const char* source_path, const char* destination_path) {
    if (!g_root) {
        return finish(Status::NotInitialized);
    }

    ParsedPath source_parsed = {};
    FileEntry* source = nullptr;
    Status status = resolve_path(source_path, &source_parsed, &source);
    if (status != Status::Ok) {
        return finish(status);
    }
    if (source->is_directory) {
        return finish(Status::IsDirectory);
    }

    ParsedPath destination_parsed = {};
    FileEntry* destination_parent = nullptr;
    const PathComponent* destination_name = nullptr;
    FileEntry* existing = nullptr;
    status = resolve_destination(
        destination_path,
        false,
        &destination_parsed,
        &destination_parent,
        &destination_name,
        &existing);
    if (status != Status::Ok) {
        return finish(status);
    }
    if (existing) {
        return finish(Status::AlreadyExists);
    }
    if (g_node_count >= RAMFS_MAX_NODES) {
        return finish(Status::TooManyNodes);
    }
    if (destination_parent->child_count >= RAMFS_MAX_CHILDREN) {
        return finish(Status::TooManyChildren);
    }
    if (source->size > RAMFS_MAX_TOTAL_FILE_BYTES - g_total_file_bytes) {
        return finish(Status::StorageLimitReached);
    }

    // Build the complete copy while it is detached. A failure therefore
    // cannot expose a partial destination entry.
    FileEntry* copy = allocate_entry(*destination_name, false);
    if (!copy) {
        return finish(
            g_node_count >= RAMFS_MAX_NODES
                ? Status::TooManyNodes
                : Status::OutOfMemory);
    }
    status = write_entry(copy, source->content, source->size);
    if (status != Status::Ok) {
        destroy_subtree(copy);
        return finish(status);
    }
    status = attach_child(destination_parent, copy);
    if (status != Status::Ok) {
        destroy_subtree(copy);
        return finish(status);
    }
    return finish(Status::Ok);
}

Status move_path(const char* source_path, const char* destination_path) {
    if (!g_root) {
        return finish(Status::NotInitialized);
    }

    ParsedPath source_parsed = {};
    FileEntry* source = nullptr;
    Status status = resolve_path(source_path, &source_parsed, &source);
    if (status != Status::Ok) {
        return finish(status);
    }
    if (source == g_root) {
        return finish(Status::RootProtected);
    }

    ParsedPath destination_parsed = {};
    FileEntry* destination_parent = nullptr;
    const PathComponent* destination_name = nullptr;
    FileEntry* existing = nullptr;
    status = resolve_destination(
        destination_path,
        source->is_directory,
        &destination_parsed,
        &destination_parent,
        &destination_name,
        &existing);
    if (status != Status::Ok) {
        return finish(status);
    }
    if (existing == source) {
        return finish(Status::Ok);
    }
    if (existing) {
        return finish(Status::AlreadyExists);
    }
    if (source->is_directory &&
        is_same_or_descendant(destination_parent, source)) {
        return finish(Status::WouldCreateCycle);
    }

    status = subtree_fits_destination(
        source, destination_parent, destination_name->length);
    if (status != Status::Ok) {
        return finish(status);
    }

    FileEntry* old_parent = source->parent;
    if (destination_parent != old_parent) {
        if (destination_parent->child_count >= RAMFS_MAX_CHILDREN) {
            return finish(Status::TooManyChildren);
        }
        status = ensure_child_capacity(destination_parent);
        if (status != Status::Ok) {
            return finish(status);
        }
    }

    // Every operation that may allocate or fail has completed. From this
    // point the move only changes bounded in-memory metadata.
    if (destination_parent != old_parent) {
        detach_child(old_parent, source);
    }
    set_entry_name(source, *destination_name);
    if (destination_parent != old_parent) {
        destination_parent->children[destination_parent->child_count++] = source;
        source->parent = destination_parent;
    }
    return finish(Status::Ok);
}

Status rename_path(const char* source_path, const char* destination_path) {
    return move_path(source_path, destination_path);
}

Status last_status() {
    return g_last_status;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotInitialized: return "not initialized";
        case Status::InvalidArgument: return "invalid argument";
        case Status::InvalidPath: return "invalid path";
        case Status::PathTooLong: return "path too long";
        case Status::NameTooLong: return "name too long";
        case Status::PathTooDeep: return "path too deep";
        case Status::NotFound: return "not found";
        case Status::AlreadyExists: return "already exists";
        case Status::NotDirectory: return "not a directory";
        case Status::IsDirectory: return "is a directory";
        case Status::DirectoryNotEmpty: return "directory not empty";
        case Status::RootProtected: return "root is protected";
        case Status::FileTooLarge: return "file too large";
        case Status::TooManyChildren: return "too many children";
        case Status::TooManyNodes: return "too many nodes";
        case Status::StorageLimitReached: return "storage limit reached";
        case Status::OutOfMemory: return "out of memory";
        case Status::BufferTooSmall: return "buffer too small";
        case Status::WouldCreateCycle: return "move would create a directory cycle";
        case Status::IterationStopped: return "iteration stopped";
    }
    return "unknown status";
}

void init_ramfs() {
    initialize_ramfs();
}

FileEntry* create_file(const char* name) {
    if (!g_root && initialize_ramfs() != Status::Ok) {
        return nullptr;
    }

    FileEntry* entry = nullptr;
    create_file_at(name, &entry);
    return entry;
}

FileEntry* create_directory(const char* name) {
    if (!g_root && initialize_ramfs() != Status::Ok) {
        return nullptr;
    }

    FileEntry* entry = nullptr;
    create_directory_at(name, &entry);
    return entry;
}

const char* read_file(const char* name) {
    ParsedPath parsed = {};
    FileEntry* entry = nullptr;
    Status status = resolve_path(name, &parsed, &entry);
    if (status != Status::Ok) {
        finish(status);
        return nullptr;
    }
    if (entry->is_directory) {
        finish(Status::IsDirectory);
        return nullptr;
    }

    finish(Status::Ok);
    return entry->content ? entry->content : g_empty_content;
}

void write_file(const char* name, const char* content) {
    if (!g_root && initialize_ramfs() != Status::Ok) {
        return;
    }

    size_t length = 0;
    Status status =
        bounded_length(content, RAMFS_MAX_FILE_SIZE, &length, Status::FileTooLarge);
    if (status != Status::Ok) {
        finish(status);
        return;
    }
    write_file_data(name, content, length, true);
}

namespace {
bool ignore_listing(const char*, const FileStat*, void*) {
    return true;
}
} // namespace

void list_files() {
    list_directory("/", ignore_listing, nullptr);
}

} // namespace fs
