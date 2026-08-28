#include "../common/dev_profile.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void expect_valid_password_profile(void) {
    static const char config[] =
        "USERNAME=user\n"
        "PASSWORD_REQUIRED=1\n"
        "PASSWORD_HASH=57FF0F93FF125DDB\n"
        "HASH_SCHEME=FNV1A64-DEV\n";
    struct ku_dev_profile_data profile = {{0}, 0U, 0};
    assert(ku_dev_profile_parse_user_config(config, &profile));
    assert(strcmp(profile.username, "user") == 0);
    assert(profile.password_required == 1);
    assert(profile.password_hash == UINT64_C(0x57FF0F93FF125DDB));
    assert(ku_dev_credential_verify(
        profile.username, "secret", profile.password_hash));
    assert(!ku_dev_credential_verify(
        profile.username, "bad", profile.password_hash));
}

static void expect_valid_no_password_profile(void) {
    static const char config[] =
        "USERNAME=dev-user\n"
        "PASSWORD_REQUIRED=0\n"
        "PASSWORD_HASH=0000000000000000\n"
        "HASH_SCHEME=FNV1A64-DEV\n";
    struct ku_dev_profile_data profile = {{0}, 0U, 1};
    assert(ku_dev_profile_parse_user_config(config, &profile));
    assert(strcmp(profile.username, "dev-user") == 0);
    assert(profile.password_required == 0);
    assert(profile.password_hash == 0U);
}

static void expect_malformed_profiles_fail_closed(void) {
    struct ku_dev_profile_data profile = {{0}, 0U, 0};

    assert(!ku_dev_profile_parse_user_config(
        "USERNAME=user\n"
        "PASSWORD_HASH=57FF0F93FF125DDB\n"
        "HASH_SCHEME=FNV1A64-DEV\n",
        &profile));

    assert(!ku_dev_profile_parse_user_config(
        "USERNAME=user\n"
        "PASSWORD_REQUIRED=1\n"
        "PASSWORD_HASH=not-a-valid-hash\n"
        "HASH_SCHEME=FNV1A64-DEV\n",
        &profile));

    assert(!ku_dev_profile_parse_user_config(
        "USERNAME=user\n"
        "PASSWORD_REQUIRED=1\n"
        "PASSWORD_HASH=57FF0F93FF125DDB\n"
        "HASH_SCHEME=UNSUPPORTED\n",
        &profile));

    assert(!ku_dev_profile_parse_user_config(
        "USERNAME=bad name\n"
        "PASSWORD_REQUIRED=0\n"
        "PASSWORD_HASH=0000000000000000\n"
        "HASH_SCHEME=FNV1A64-DEV\n",
        &profile));

    assert(!ku_dev_profile_parse_user_config(
        "USERNAME=user\n"
        "PASSWORD_REQUIRED=0\n"
        "PASSWORD_HASH=57FF0F93FF125DDB\n"
        "HASH_SCHEME=FNV1A64-DEV\n",
        &profile));
}

static void expect_locale_validation(void) {
    char locale[KU_DEV_PROFILE_LOCALE_CAPACITY] = {0};
    assert(ku_dev_profile_parse_locale("LANG=en-US\n", locale));
    assert(strcmp(locale, "en-US") == 0);
    assert(ku_dev_profile_parse_locale("LANG=pl-PL\n", locale));
    assert(strcmp(locale, "pl-PL") == 0);
    assert(!ku_dev_profile_parse_locale("LANG=xx-XX\n", locale));
    assert(!ku_dev_profile_parse_locale("BROKEN=pl-PL\n", locale));
}

int main(void) {
    expect_valid_password_profile();
    expect_valid_no_password_profile();
    expect_malformed_profiles_fail_closed();
    expect_locale_validation();
    puts("installer profile validation tests: PASS");
    return 0;
}
