#ifndef KUROGANE_SDK_TEST_H
#define KUROGANE_SDK_TEST_H

#include <kurogane/status.h>

/*
 * Header-only test support for SDK consumers.  The core deliberately avoids
 * stdio, allocation and other hosted-runtime facilities.  Environments decide
 * how to present results by installing a reporter callback.
 */

#define KU_TEST_EXIT_SUCCESS 0
#define KU_TEST_EXIT_FAILURE 1

enum ku_test_event_kind {
    KU_TEST_EVENT_SUITE_BEGIN = 1,
    KU_TEST_EVENT_GROUP_BEGIN = 2,
    KU_TEST_EVENT_ASSERTION_FAILURE = 3,
    KU_TEST_EVENT_GROUP_END = 4,
    KU_TEST_EVENT_SUITE_END = 5
};

typedef struct ku_test_event {
    enum ku_test_event_kind kind;
    const char* suite_name;
    const char* group_name;
    const char* expression;
    const char* file;
    uint32_t line;
    uint32_t groups;
    uint32_t groups_failed;
    uint32_t assertions;
    uint32_t failures;
    uint32_t group_assertions;
    uint32_t group_failures;
} ku_test_event;

typedef void (*ku_test_report_fn)(const ku_test_event* event, void* user_data);

typedef struct ku_test_context {
    const char* suite_name;
    const char* group_name;
    ku_test_report_fn report;
    void* report_user_data;
    uint32_t groups;
    uint32_t groups_failed;
    uint32_t assertions;
    uint32_t failures;
    uint32_t group_assertions;
    uint32_t group_failures;
    uint32_t group_active;
    uint32_t finished;
} ku_test_context;

static inline void ku_test_emit(ku_test_context* context,
                                enum ku_test_event_kind kind,
                                const char* expression,
                                const char* file,
                                uint32_t line) {
    ku_test_event event;

    if (context->report == NULL) {
        return;
    }

    event.kind = kind;
    event.suite_name = context->suite_name;
    event.group_name = context->group_name;
    event.expression = expression;
    event.file = file;
    event.line = line;
    event.groups = context->groups;
    event.groups_failed = context->groups_failed;
    event.assertions = context->assertions;
    event.failures = context->failures;
    event.group_assertions = context->group_assertions;
    event.group_failures = context->group_failures;
    context->report(&event, context->report_user_data);
}

static inline ku_status_t ku_test_initialize(ku_test_context* context,
                                             const char* suite_name,
                                             ku_test_report_fn report,
                                             void* report_user_data) {
    if (context == NULL || suite_name == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }

    context->suite_name = suite_name;
    context->group_name = NULL;
    context->report = report;
    context->report_user_data = report_user_data;
    context->groups = 0;
    context->groups_failed = 0;
    context->assertions = 0;
    context->failures = 0;
    context->group_assertions = 0;
    context->group_failures = 0;
    context->group_active = 0;
    context->finished = 0;

    ku_test_emit(context, KU_TEST_EVENT_SUITE_BEGIN, NULL, NULL, 0);
    return KU_STATUS_OK;
}

static inline ku_status_t ku_test_group_begin(ku_test_context* context,
                                              const char* group_name) {
    if (context == NULL || group_name == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    if (context->finished != 0 || context->group_active != 0) {
        return KU_STATUS_BAD_STATE;
    }

    context->group_name = group_name;
    context->group_assertions = 0;
    context->group_failures = 0;
    context->group_active = 1;
    ku_test_emit(context, KU_TEST_EVENT_GROUP_BEGIN, NULL, NULL, 0);
    return KU_STATUS_OK;
}

static inline ku_status_t ku_test_group_end(ku_test_context* context) {
    if (context == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    if (context->finished != 0 || context->group_active == 0) {
        return KU_STATUS_BAD_STATE;
    }

    ++context->groups;
    if (context->group_failures != 0) {
        ++context->groups_failed;
    }
    ku_test_emit(context, KU_TEST_EVENT_GROUP_END, NULL, NULL, 0);
    context->group_name = NULL;
    context->group_assertions = 0;
    context->group_failures = 0;
    context->group_active = 0;
    return KU_STATUS_OK;
}

static inline int ku_test_record_assertion(ku_test_context* context,
                                           int condition,
                                           const char* expression,
                                           const char* file,
                                           uint32_t line) {
    if (context == NULL || expression == NULL || file == NULL ||
        context->finished != 0) {
        return 0;
    }

    ++context->assertions;
    if (context->group_active != 0) {
        ++context->group_assertions;
    }

    if (condition != 0) {
        return 1;
    }

    ++context->failures;
    if (context->group_active != 0) {
        ++context->group_failures;
    }
    ku_test_emit(context, KU_TEST_EVENT_ASSERTION_FAILURE, expression, file,
                 line);
    return 0;
}

static inline int ku_test_finish(ku_test_context* context) {
    if (context == NULL) {
        return KU_TEST_EXIT_FAILURE;
    }
    if (context->finished != 0) {
        return context->failures == 0 ? KU_TEST_EXIT_SUCCESS
                                      : KU_TEST_EXIT_FAILURE;
    }

    if (context->group_active != 0) {
        (void)ku_test_group_end(context);
    }
    context->finished = 1;
    ku_test_emit(context, KU_TEST_EVENT_SUITE_END, NULL, NULL, 0);
    return context->failures == 0 ? KU_TEST_EXIT_SUCCESS
                                  : KU_TEST_EXIT_FAILURE;
}

#define KU_TEST_ASSERT(context, expression)                                  \
    ((void)ku_test_record_assertion((context), ((expression) ? 1 : 0),       \
                                    #expression, __FILE__,                   \
                                    (uint32_t)__LINE__))

#define KU_TEST_ASSERT_EQ(context, left, right)                              \
    KU_TEST_ASSERT((context), ((left) == (right)))

#define KU_TEST_ASSERT_NE(context, left, right)                              \
    KU_TEST_ASSERT((context), ((left) != (right)))

#endif
