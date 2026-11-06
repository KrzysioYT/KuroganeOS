#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../userspace/gui/files/rename.h"

static void test_rename_policy(void) {
    assert(flux_files_rename_check(KU_FILE_TYPE_REGULAR, "notes.txt", "notes.txt", 64U) ==
           FLUX_FILES_RENAME_UNCHANGED);
    assert(flux_files_rename_check(KU_FILE_TYPE_REGULAR, "notes.txt", "notes-2.txt", 64U) ==
           FLUX_FILES_RENAME_READY);
    assert(flux_files_rename_check(KU_FILE_TYPE_DIRECTORY, "Projects", "Projects 2", 64U) ==
           FLUX_FILES_RENAME_READY);
    assert(flux_files_rename_check(KU_FILE_TYPE_DIRECTORY, "folder", ".config", 64U) ==
           FLUX_FILES_RENAME_READY);
    assert(flux_files_rename_check(KU_FILE_TYPE_MOUNT_POINT, "system", "config", 64U) ==
           FLUX_FILES_RENAME_INVALID);
    assert(flux_files_rename_check(KU_FILE_TYPE_DEVICE, "disk", "other", 64U) ==
           FLUX_FILES_RENAME_INVALID);
    assert(flux_files_rename_check(KU_FILE_TYPE_UNKNOWN, "unknown", "new", 64U) ==
           FLUX_FILES_RENAME_INVALID);
    assert(flux_files_rename_check(KU_FILE_TYPE_REGULAR, "source", "../escape", 64U) ==
           FLUX_FILES_RENAME_INVALID);
    assert(flux_files_rename_check(KU_FILE_TYPE_REGULAR, "source", "a/b", 64U) ==
           FLUX_FILES_RENAME_INVALID);
    assert(flux_files_rename_check(KU_FILE_TYPE_DIRECTORY, "source", "a\\b", 64U) ==
           FLUX_FILES_RENAME_INVALID);
    assert(flux_files_rename_check(KU_FILE_TYPE_DIRECTORY, "source", ".", 64U) ==
           FLUX_FILES_RENAME_INVALID);
    assert(flux_files_rename_check(KU_FILE_TYPE_DIRECTORY, "source", "", 64U) ==
           FLUX_FILES_RENAME_INVALID);
    assert(flux_files_rename_check(KU_FILE_TYPE_REGULAR, "source", "source ", 64U) ==
           FLUX_FILES_RENAME_INVALID);
    assert(flux_files_rename_check(KU_FILE_TYPE_REGULAR, "", "name", 64U) ==
           FLUX_FILES_RENAME_INVALID);
    assert(flux_files_rename_check(KU_FILE_TYPE_REGULAR, "..", "name", 64U) ==
           FLUX_FILES_RENAME_INVALID);
    assert(flux_files_rename_check(KU_FILE_TYPE_REGULAR, "dir/child", "name", 64U) ==
           FLUX_FILES_RENAME_INVALID);
    assert(flux_files_rename_check(KU_FILE_TYPE_REGULAR, NULL, "name", 64U) ==
           FLUX_FILES_RENAME_INVALID);
    assert(flux_files_rename_check(KU_FILE_TYPE_REGULAR, "source", NULL, 64U) ==
           FLUX_FILES_RENAME_INVALID);
    {
        char unbounded[4] = {'a', 'b', 'c', 'd'};
        assert(flux_files_rename_check(KU_FILE_TYPE_REGULAR, unbounded, "name", 4U) ==
               FLUX_FILES_RENAME_INVALID);
        assert(flux_files_rename_check(KU_FILE_TYPE_REGULAR, "source", unbounded, 4U) ==
               FLUX_FILES_RENAME_INVALID);
    }
}

int main(void) {
    test_rename_policy();
    puts("Flux Files safe rename policy: PASS");
    return 0;
}
