#ifndef KUROGANE_DEV_LOGIN_PROFILE_H
#define KUROGANE_DEV_LOGIN_PROFILE_H

#include <kurogane/filesystem.h>
#include "../../common/dev_profile.h"

typedef enum ku_dev_login_kind {
    KU_DEV_LOGIN_BLOCKED = 0,
    KU_DEV_LOGIN_LIVE,
    KU_DEV_LOGIN_INSTALLED
} ku_dev_login_kind;

typedef struct ku_dev_login_profile {
    ku_dev_login_kind kind;
    char locale[KU_DEV_PROFILE_LOCALE_CAPACITY];
    struct ku_dev_profile_data credentials;
} ku_dev_login_profile;

/* Only a definite NOT_FOUND can take part in selecting a live profile.
 * I/O errors, oversized files and embedded NULs must not turn into a
 * successful parse of a short prefix or into passwordless login.
 */
static inline ku_status_t ku_dev_login_read_config(
    const char* path, char* output, size_t capacity) {
    const ku_result_t opened = ku_open(
        path, ku_dev_profile_strlen(path), KU_OPEN_READ);
    if (opened == KU_STATUS_NOT_FOUND) return KU_STATUS_NOT_FOUND;
    if (opened <= 0) return KU_STATUS_IO_ERROR;

    const ku_handle_t handle = (ku_handle_t)opened;
    ku_status_t status = KU_STATUS_CORRUPT_DATA;
    size_t used = 0U;
    while (used < capacity) {
        const ku_result_t count = ku_read(handle, output + used, capacity - used);
        if (count < 0 || (uint64_t)count > capacity - used) break;
        if (count == 0) {
            if (used != 0U) {
                output[used] = '\0';
                status = KU_STATUS_OK;
            }
            break;
        }
        const size_t end = used + (size_t)count;
        while (used < end && output[used] != '\0') ++used;
        if (used != end) break;
    }
    if (ku_close(handle) != KU_STATUS_OK) return KU_STATUS_IO_ERROR;
    return status;
}

static inline void ku_dev_login_load_profile(ku_dev_login_profile* profile) {
    char user[384];
    char locale[64];
    ku_file_stat info;
    const ku_status_t user_status =
        ku_dev_login_read_config("/etc/user.cfg", user, sizeof(user));
    const ku_status_t locale_status =
        ku_dev_login_read_config("/etc/locale.cfg", locale, sizeof(locale));
    const ku_dev_login_profile defaults = {
        KU_DEV_LOGIN_BLOCKED, "en-US", {"developer", 0U, 0}
    };
    *profile = defaults;
    const int locale_valid = locale_status == KU_STATUS_OK &&
        ku_dev_profile_parse_locale(locale, profile->locale);

    if (user_status == KU_STATUS_OK && locale_valid &&
        ku_dev_profile_parse_user_config(user, &profile->credentials)) {
        profile->kind = KU_DEV_LOGIN_INSTALLED;
        return;
    }

    /* The clean Foundation image and read-only Try both lack these files.
     * The installer creates first.run and the kernel replaces it with
     * install.ok on first boot. A missing credential on either installed
     * boot is damage to the account profile, not a fresh developer session.
     */
    if (user_status == KU_STATUS_NOT_FOUND &&
        locale_status == KU_STATUS_NOT_FOUND &&
        ku_file_stat_path("/etc/first.run", sizeof("/etc/first.run") - 1U, &info) ==
            KU_STATUS_NOT_FOUND &&
        ku_file_stat_path("/etc/install.ok", sizeof("/etc/install.ok") - 1U, &info) ==
            KU_STATUS_NOT_FOUND) {
        profile->kind = KU_DEV_LOGIN_LIVE;
    }
}

#endif
