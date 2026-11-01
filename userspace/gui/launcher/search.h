#ifndef KUROGANE_FLUX_LAUNCHER_SEARCH_H
#define KUROGANE_FLUX_LAUNCHER_SEARCH_H

#include <stddef.h>
#include <stdint.h>

#define FLUX_LAUNCHER_SEARCH_CAPACITY 32U

// The installed native app catalog currently uses ASCII names/paths. Match
// those case-insensitively without locale state or allocating a second copy.
static inline unsigned char flux_search_fold(unsigned char value) {
    return value >= (unsigned char)'A' && value <= (unsigned char)'Z'
        ? (unsigned char)(value + ('a' - 'A')) : value;
}

static inline int flux_search_contains(const char* haystack, const char* query) {
    size_t start;
    if (haystack == (const char*)0 || query == (const char*)0) return 0;
    if (query[0] == '\0') return 1;
    for (start = 0U; haystack[start] != '\0'; ++start) {
        size_t offset = 0U;
        while (query[offset] != '\0' && haystack[start + offset] != '\0' &&
               flux_search_fold((unsigned char)haystack[start + offset]) ==
                   flux_search_fold((unsigned char)query[offset])) {
            ++offset;
        }
        if (query[offset] == '\0') return 1;
    }
    return 0;
}

static inline int flux_search_matches(
    const char* label, const char* subtitle, const char* executable,
    const char* query) {
    return flux_search_contains(label, query) ||
        flux_search_contains(subtitle, query) ||
        flux_search_contains(executable, query);
}

// Returns 1 only if the query was actually changed. A non-ASCII input byte is
// ignored for now; this protects the search buffer until the planned UTF-8
// text-input layer is integrated with the native widget system.
static inline int flux_search_edit(
    char* query, size_t capacity, uint32_t character, int backspace,
    int clear, int modified) {
    size_t used = 0U;
    if (query == (char*)0 || capacity < 2U) return 0;
    while (used < capacity && query[used] != '\0') ++used;
    if (used == capacity) return 0;
    if (clear) {
        if (used == 0U) return 0;
        query[0] = '\0';
        return 1;
    }
    if (backspace) {
        if (used == 0U) return 0;
        query[used - 1U] = '\0';
        return 1;
    }
    if (modified || character < 32U || character > 126U ||
        used + 1U >= capacity) return 0;
    query[used] = (char)character;
    query[used + 1U] = '\0';
    return 1;
}

#endif
