#ifndef KUROGANE_ANVIL_DATABASE_COMPACTION_H
#define KUROGANE_ANVIL_DATABASE_COMPACTION_H

#include <stddef.h>
#include <string.h>

static int anvil_database_name_matches(
    const char* line,
    size_t line_size,
    const char* package_name) {
    size_t name_size = 0U;
    const size_t expected = strlen(package_name);
    while (name_size < line_size && line[name_size] != '|') ++name_size;
    return name_size == expected &&
        name_size < line_size &&
        memcmp(line, package_name, name_size) == 0;
}

static int anvil_database_compact(
    const char* database,
    const char* package_name,
    const char* replacement,
    char* output,
    size_t capacity,
    size_t* output_size) {
    size_t source = 0U;
    size_t used = 0U;
    const size_t replacement_size = replacement != NULL ? strlen(replacement) : 0U;

    if (database == NULL || package_name == NULL || package_name[0] == '\0' ||
        replacement == NULL || replacement_size == 0U || output == NULL ||
        capacity == 0U || output_size == NULL) return 0;

    while (database[source] != '\0') {
        const size_t line_start = source;
        size_t line_end = source;
        while (database[line_end] != '\0' &&
               database[line_end] != '\r' &&
               database[line_end] != '\n') ++line_end;
        if (line_end > line_start &&
            !anvil_database_name_matches(
                database + line_start,
                line_end - line_start,
                package_name)) {
            const size_t line_size = line_end - line_start;
            if (used + line_size + 1U >= capacity) return 0;
            memcpy(output + used, database + line_start, line_size);
            used += line_size;
            output[used++] = '\n';
        }
        while (database[line_end] == '\r' || database[line_end] == '\n') ++line_end;
        source = line_end;
    }

    if (used + replacement_size + 1U > capacity) return 0;
    memcpy(output + used, replacement, replacement_size);
    used += replacement_size;
    output[used] = '\0';
    *output_size = used;
    return 1;
}

#endif
