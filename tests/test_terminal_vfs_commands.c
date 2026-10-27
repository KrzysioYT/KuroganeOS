#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kurogane/filesystem.h>

/* Host-side VFS stand-in: exercise the production shell parser and directory
 * commands without needing a kernel, disk image or privileged syscalls. */
static int g_open_count;
static int g_close_count;
static size_t g_entry_index;

static ku_result_t mock_file_open(const char* path, size_t size, uint64_t flags) {
    assert(size == strlen(path));
    assert((flags & KU_FILE_OPEN_DIRECTORY) != 0U);
    ++g_open_count;
    if (strcmp(path, "/") == 0) return 1;
    if (strcmp(path, "/apps") == 0) {
        g_entry_index = 0U;
        return 2;
    }
    return -1;
}

static ku_status_t mock_file_close(ku_file_t handle) {
    assert(handle == 1U || handle == 2U);
    ++g_close_count;
    return KU_STATUS_OK;
}

static ku_status_t mock_file_readdir(ku_file_t handle, ku_directory_entry* entry) {
    assert(handle == 1U || handle == 2U);
    if (g_entry_index++ > 1U) return KU_STATUS_END_OF_STREAM;
    entry->type = g_entry_index == 1U ? KU_FILE_TYPE_DIRECTORY : KU_FILE_TYPE_REGULAR;
    strcpy(entry->name, g_entry_index == 1U ? "docs" : "terminal");
    entry->name_length = (uint32_t)strlen(entry->name);
    return KU_STATUS_OK;
}

#define ku_file_open_ex mock_file_open
#define ku_file_close mock_file_close
#define ku_file_readdir mock_file_readdir
#include "../userspace/common/shell_core.h"
#undef ku_file_open_ex
#undef ku_file_close
#undef ku_file_readdir

typedef struct log_buffer {
    char text[1024];
} log_buffer;

static void emit_line(void* context, const char* line) {
    log_buffer* output = (log_buffer*)context;
    size_t used = strlen(output->text);
    size_t count = strlen(line);
    assert(used + count + 2U < sizeof(output->text));
    memcpy(output->text + used, line, count);
    output->text[used + count] = '\n';
    output->text[used + count + 1U] = '\0';
}

int main(void) {
    flux_shell_state shell;
    log_buffer output = {{0}};
    const flux_shell_io io = {emit_line, &output};
    flux_shell_initialize(&shell);

    (void)flux_shell_execute(&shell, &io, "cd /apps");
    assert(shell.last_status == 0);
    assert(strcmp(shell.cwd, "/apps") == 0);
    assert(g_close_count == 1);

    (void)flux_shell_execute(&shell, &io, "ls");
    assert(shell.last_status == 0);
    assert(strstr(output.text, "[DIR] docs\n") != NULL);
    assert(strstr(output.text, "      terminal\n") != NULL);
    assert(g_close_count == 2);

    (void)flux_shell_execute(&shell, &io, "cd /missing");
    assert(shell.last_status == 1);
    assert(strcmp(shell.cwd, "/apps") == 0);
    assert(strstr(output.text, "cd: directory unavailable") != NULL);

    (void)flux_shell_execute(&shell, &io, "ls /missing");
    assert(shell.last_status == 1);
    assert(strstr(output.text, "ls: cannot open directory") != NULL);

    (void)flux_shell_execute(&shell, &io, "cd ..");
    assert(shell.last_status == 0);
    assert(strcmp(shell.cwd, "/") == 0);
    assert(g_open_count == 5);
    assert(g_close_count == 3);

    puts("Terminal Ring-3 ls/cd VFS behavior: PASS");
    return 0;
}
