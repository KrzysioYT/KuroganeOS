#pragma once

// Dock readiness/focus identifies an installed Ring-3 application, not just a
// window that happens to use its public title. No allocation or libc needed.
namespace windowing::app_identity {

inline bool exact_text(const char* actual, const char* expected) {
    if (actual == nullptr || expected == nullptr) return false;
    for (; *expected != '\0'; ++expected, ++actual) {
        if (*actual != *expected) return false;
    }
    return *actual == '\0';
}

inline bool matches(const char* title, const char* expected_title,
                    const char* executable, const char* expected_executable) {
    return exact_text(title, expected_title) &&
        exact_text(executable, expected_executable);
}

} // namespace windowing::app_identity
