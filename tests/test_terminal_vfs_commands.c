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
    if (strcmp(path, "/") == 0) {
        g_entry_index = 0U;
        return 1;
    }
    if (strcmp(path, "/apps") == 0 || strcmp(path, "/apps/") == 0) {
        g_entry_index = 0U;
        return 2;
    }
    if (strcmp(path, "/docs/") == 0) {
        g_entry_index = 0U;
        return 3;
    }
    return -1;
}

static ku_status_t mock_file_close(ku_file_t handle) {
    assert(handle == 1U || handle == 2U || handle == 3U);
    ++g_close_count;
    return KU_STATUS_OK;
}

static ku_status_t mock_file_readdir(ku_file_t handle, ku_directory_entry* entry) {
    static const char* const root[] = {"apps", "docs", "README"};
    static const char* const apps[] = {"docs", "terminal", "test", "themes"};
    static const char* const docs[] = {"guide.txt", "images"};
    const char* name;
    if (handle == 1U) {
        if (g_entry_index >= sizeof(root) / sizeof(root[0])) {
            return KU_STATUS_END_OF_STREAM;
        }
        name = root[g_entry_index];
        entry->type = g_entry_index < 2U ? KU_FILE_TYPE_DIRECTORY : KU_FILE_TYPE_REGULAR;
    } else if (handle == 2U) {
        if (g_entry_index >= sizeof(apps) / sizeof(apps[0])) {
            return KU_STATUS_END_OF_STREAM;
        }
        name = apps[g_entry_index];
        entry->type = g_entry_index == 0U || g_entry_index == 3U
            ? KU_FILE_TYPE_DIRECTORY : KU_FILE_TYPE_REGULAR;
    } else {
        assert(handle == 3U);
        if (g_entry_index >= sizeof(docs) / sizeof(docs[0])) {
            return KU_STATUS_END_OF_STREAM;
        }
        name = docs[g_entry_index];
        entry->type = g_entry_index == 1U ? KU_FILE_TYPE_DIRECTORY : KU_FILE_TYPE_REGULAR;
    }
    ++g_entry_index;
    strcpy(entry->name, name);
    entry->name_length = (uint32_t)strlen(entry->name);
    return KU_STATUS_OK;
}

#define ku_file_open_ex mock_file_open
#define ku_file_close mock_file_close
#define ku_file_readdir mock_file_readdir
#include "../userspace/common/shell_core.h"
#include "../userspace/common/shell_path_completion.h"
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

static void test_path_completion(const flux_shell_state* shell) {
    char input[80];
    size_t cursor;

    strcpy(input, "cd /do");
    cursor = strlen(input);
    assert(ku_shell_complete_path(shell, input, sizeof(input), &cursor) ==
           KU_SHELL_COMPLETION_APPLIED);
    assert(strcmp(input, "cd /docs/") == 0);
    assert(cursor == strlen(input));

    strcpy(input, "cat /REA");
    cursor = strlen(input);
    assert(ku_shell_complete_path(shell, input, sizeof(input), &cursor) ==
           KU_SHELL_COMPLETION_APPLIED);
    assert(strcmp(input, "cat /README ") == 0);

    strcpy(input, "ls /REA");
    cursor = strlen(input);
    assert(ku_shell_complete_path(shell, input, sizeof(input), &cursor) ==
           KU_SHELL_COMPLETION_NONE);

    strcpy(input, "run ter");
    cursor = strlen(input);
    assert(ku_shell_complete_path(shell, input, sizeof(input), &cursor) ==
           KU_SHELL_COMPLETION_APPLIED);
    assert(strcmp(input, "run terminal ") == 0);

    strcpy(input, "run te");
    cursor = strlen(input);
    assert(ku_shell_complete_path(shell, input, sizeof(input), &cursor) ==
           KU_SHELL_COMPLETION_AMBIGUOUS);
    assert(strcmp(input, "run te") == 0);

    strcpy(input, "open th");
    cursor = strlen(input);
    assert(ku_shell_complete_path(shell, input, sizeof(input), &cursor) ==
           KU_SHELL_COMPLETION_APPLIED);
    assert(strcmp(input, "open themes/") == 0);

    strcpy(input, "cat /docs/g");
    cursor = strlen(input);
    assert(ku_shell_complete_path(shell, input, sizeof(input), &cursor) ==
           KU_SHELL_COMPLETION_APPLIED);
    assert(strcmp(input, "cat /docs/guide.txt ") == 0);

    strcpy(input, "cat /no-such-dir/test");
    cursor = strlen(input);
    assert(ku_shell_complete_path(shell, input, sizeof(input), &cursor) ==
           KU_SHELL_COMPLETION_NONE);

    strcpy(input, "echo /REA");
    cursor = strlen(input);
    assert(ku_shell_complete_path(shell, input, sizeof(input), &cursor) ==
           KU_SHELL_COMPLETION_NONE);

    strcpy(input, "run ter");
    cursor = 6U;
    assert(ku_shell_complete_path(shell, input, sizeof(input), &cursor) ==
           KU_SHELL_COMPLETION_NONE);

    {
        char tight[9] = "run ter";
        cursor = strlen(tight);
        assert(ku_shell_complete_path(shell, tight, sizeof(tight), &cursor) ==
               KU_SHELL_COMPLETION_NONE);
        assert(strcmp(tight, "run ter") == 0);
    }
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

    test_path_completion(&shell);

    puts("Terminal Ring-3 ls/cd and path completion: PASS");
    return 0;
}
