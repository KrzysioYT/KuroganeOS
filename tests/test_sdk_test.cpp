#include <kurogane/test_host.h>

#include <cstdio>
#include <cstring>

namespace {

int run_passing_suite() {
    ku_test_context test{};
    ku_test_host_reporter reporter{};

    if (ku_test_host_initialize(&test, &reporter, stdout,
                                "sdk-test-framework") != KU_STATUS_OK) {
        return 10;
    }

    if (ku_test_group_begin(&test, "assertions") != KU_STATUS_OK) {
        return 11;
    }
    const int answer = 42;
    KU_TEST_ASSERT(&test, answer > 0);
    KU_TEST_ASSERT_EQ(&test, answer, 42);
    KU_TEST_ASSERT_NE(&test, answer, 0);
    if (ku_test_group_end(&test) != KU_STATUS_OK) {
        return 12;
    }

    if (ku_test_group_begin(&test, "grouping") != KU_STATUS_OK) {
        return 13;
    }
    KU_TEST_ASSERT_EQ(&test, test.groups, UINT32_C(1));
    if (ku_test_group_end(&test) != KU_STATUS_OK) {
        return 14;
    }

    const int result = ku_test_finish(&test);
    if (result != KU_TEST_EXIT_SUCCESS || test.groups != 2 ||
        test.groups_failed != 0 || test.assertions != 4 ||
        test.failures != 0) {
        return 15;
    }
    return 0;
}

int report_contains(FILE* stream, const char* expected) {
    char report[1024];

    if (fflush(stream) != 0 || fseek(stream, 0, SEEK_SET) != 0) {
        return 0;
    }
    const size_t size = fread(report, 1, sizeof(report) - 1, stream);
    report[size] = '\0';
    return strstr(report, expected) != nullptr;
}

int verify_failure_exit_and_report() {
    FILE* stream = tmpfile();
    if (stream == nullptr) {
        return 20;
    }

    ku_test_context test{};
    ku_test_host_reporter reporter{};
    if (ku_test_host_initialize(&test, &reporter, stream,
                                "expected-failure") != KU_STATUS_OK ||
        ku_test_group_begin(&test, "known failure") != KU_STATUS_OK) {
        fclose(stream);
        return 21;
    }

    KU_TEST_ASSERT(&test, 2 + 2 == 5);
    if (ku_test_group_end(&test) != KU_STATUS_OK) {
        fclose(stream);
        return 22;
    }

    const int result = ku_test_finish(&test);
    const int report_is_clear =
        report_contains(stream, "[  FAILED  ] known failure") &&
        report_contains(stream, "2 + 2 == 5") &&
        report_contains(
            stream,
            "[  FAILED  ] expected-failure: 1 assertion failures in 1 groups");
    fclose(stream);

    if (result != KU_TEST_EXIT_FAILURE || test.groups != 1 ||
        test.groups_failed != 1 || test.assertions != 1 ||
        test.failures != 1 || !report_is_clear) {
        return 23;
    }
    return 0;
}

}  // namespace

int main() {
    const int passing_result = run_passing_suite();
    if (passing_result != 0) {
        return passing_result;
    }

    const int failure_result = verify_failure_exit_and_report();
    if (failure_result != 0) {
        return failure_result;
    }

    std::puts("[selftest] SDK test framework PASS");
    return 0;
}
