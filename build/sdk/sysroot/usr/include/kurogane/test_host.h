#ifndef KUROGANE_SDK_TEST_HOST_H
#define KUROGANE_SDK_TEST_HOST_H

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 0
#error "kurogane/test_host.h requires a hosted C or C++ implementation"
#endif

#include <stdio.h>

#include <kurogane/test.h>

typedef struct ku_test_host_reporter {
    FILE* stream;
} ku_test_host_reporter;

static inline const char* ku_test_host_group_name(
    const ku_test_event* event) {
    return event->group_name == NULL ? "<ungrouped>" : event->group_name;
}

static inline void ku_test_host_report(const ku_test_event* event,
                                       void* user_data) {
    ku_test_host_reporter* reporter = (ku_test_host_reporter*)user_data;
    FILE* stream;

    if (event == NULL || reporter == NULL) {
        return;
    }
    stream = reporter->stream == NULL ? stderr : reporter->stream;

    switch (event->kind) {
        case KU_TEST_EVENT_SUITE_BEGIN:
            (void)fprintf(stream, "[==========] suite %s\n",
                          event->suite_name);
            break;
        case KU_TEST_EVENT_GROUP_BEGIN:
            (void)fprintf(stream, "[ RUN      ] %s\n",
                          ku_test_host_group_name(event));
            break;
        case KU_TEST_EVENT_ASSERTION_FAILURE:
            (void)fprintf(stream, "[  ASSERT  ] %s: %s:%lu: %s\n",
                          ku_test_host_group_name(event), event->file,
                          (unsigned long)event->line, event->expression);
            break;
        case KU_TEST_EVENT_GROUP_END:
            if (event->group_failures == 0) {
                (void)fprintf(stream, "[       OK ] %s (%lu assertions)\n",
                              ku_test_host_group_name(event),
                              (unsigned long)event->group_assertions);
            } else {
                (void)fprintf(
                    stream,
                    "[  FAILED  ] %s (%lu assertions, %lu failures)\n",
                    ku_test_host_group_name(event),
                    (unsigned long)event->group_assertions,
                    (unsigned long)event->group_failures);
            }
            break;
        case KU_TEST_EVENT_SUITE_END:
            (void)fprintf(stream,
                          "[==========] %s: %lu groups, %lu assertions\n",
                          event->suite_name, (unsigned long)event->groups,
                          (unsigned long)event->assertions);
            if (event->failures == 0) {
                (void)fprintf(stream, "[  PASSED  ] %s\n",
                              event->suite_name);
            } else {
                (void)fprintf(
                    stream,
                    "[  FAILED  ] %s: %lu assertion failures in %lu groups\n",
                    event->suite_name, (unsigned long)event->failures,
                    (unsigned long)event->groups_failed);
            }
            break;
        default:
            break;
    }
}

static inline ku_status_t ku_test_host_initialize(
    ku_test_context* context,
    ku_test_host_reporter* reporter,
    FILE* stream,
    const char* suite_name) {
    if (reporter == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }

    reporter->stream = stream == NULL ? stderr : stream;
    return ku_test_initialize(context, suite_name, ku_test_host_report,
                              reporter);
}

#endif
