#include "../../runtime/user.h"

#include <kurogane/event_broker.h>

#define EVENTD_MAX_CLIENTS 8U
#define EVENTD_MAX_SUBSCRIPTIONS 32U

typedef struct eventd_client {
    ku_service_connection_t connection;
    uint64_t pid;
    int active;
} eventd_client;

typedef struct eventd_subscription {
    ku_event_handle_t event;
    uint64_t subscriber_pid;
    char topic[KU_EVENT_BROKER_TOPIC_CAPACITY];
    int active;
} eventd_subscription;

static eventd_client clients[EVENTD_MAX_CLIENTS];
static eventd_subscription subscriptions[EVENTD_MAX_SUBSCRIPTIONS];
static int accept_wait_reported;
static int loop_resume_reported;

static int topic_character_valid(char value) {
    return (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') ||
        value == '.' || value == '_' || value == '-';
}

static int topic_valid(const char* topic) {
    size_t index = 0U;
    if (topic == (const char*)0 || topic[0] == '\0') return 0;
    while (index < KU_EVENT_BROKER_TOPIC_CAPACITY) {
        const char value = topic[index];
        if (value == '\0') return 1;
        if (!topic_character_valid(value)) return 0;
        ++index;
    }
    return 0;
}

static int topic_equal(const char* left, const char* right) {
    size_t index = 0U;
    while (index < KU_EVENT_BROKER_TOPIC_CAPACITY) {
        if (left[index] != right[index]) return 0;
        if (left[index] == '\0') return 1;
        ++index;
    }
    return 0;
}

static void topic_copy(char* destination, const char* source) {
    size_t index = 0U;
    while (index < KU_EVENT_BROKER_TOPIC_CAPACITY) {
        destination[index] = source[index];
        if (source[index] == '\0') {
            ++index;
            break;
        }
        ++index;
    }
    while (index < KU_EVENT_BROKER_TOPIC_CAPACITY) destination[index++] = '\0';
}

static eventd_subscription* find_subscription(uint64_t pid, const char* topic) {
    size_t index = 0U;
    for (; index < EVENTD_MAX_SUBSCRIPTIONS; ++index) {
        eventd_subscription* subscription = &subscriptions[index];
        if (subscription->active && subscription->subscriber_pid == pid &&
            topic_equal(subscription->topic, topic)) return subscription;
    }
    return (eventd_subscription*)0;
}

static eventd_subscription* reserve_subscription(void) {
    size_t index = 0U;
    for (; index < EVENTD_MAX_SUBSCRIPTIONS; ++index) {
        if (!subscriptions[index].active) return &subscriptions[index];
    }
    return (eventd_subscription*)0;
}

static void clear_subscription(eventd_subscription* subscription) {
    size_t index = 0U;
    if (subscription == (eventd_subscription*)0) return;
    subscription->event = 0U;
    subscription->subscriber_pid = 0U;
    subscription->active = 0;
    for (; index < KU_EVENT_BROKER_TOPIC_CAPACITY; ++index) subscription->topic[index] = '\0';
}

static void cleanup_subscriber(uint64_t pid) {
    size_t index = 0U;
    if (pid == 0U) return;
    for (; index < EVENTD_MAX_SUBSCRIPTIONS; ++index) {
        eventd_subscription* subscription = &subscriptions[index];
        if (!subscription->active || subscription->subscriber_pid != pid) continue;
        (void)ku_event_close(subscription->event);
        clear_subscription(subscription);
    }
}

static ku_status_t subscribe(uint64_t subscriber_pid, const char* topic, uint64_t* event_value) {
    eventd_subscription* existing;
    eventd_subscription* subscription;
    ku_event_handle_t event;
    ku_result_t created;
    ku_status_t grant_status;

    if (event_value == (uint64_t*)0) return KU_STATUS_INVALID_ARGUMENT;
    *event_value = 0U;
    existing = find_subscription(subscriber_pid, topic);
    if (existing != (eventd_subscription*)0) {
        *event_value = existing->event;
        return KU_STATUS_ALREADY_EXISTS;
    }
    subscription = reserve_subscription();
    if (subscription == (eventd_subscription*)0) return KU_STATUS_OUT_OF_MEMORY;
    created = ku_event_create(KU_EVENT_AUTO_RESET, 0);
    if (created <= 0) return (ku_status_t)created;
    event = (ku_event_handle_t)created;
    grant_status = ku_event_grant(event, subscriber_pid);
    if (grant_status != KU_STATUS_OK) {
        (void)ku_event_close(event);
        return grant_status;
    }
    subscription->event = event;
    subscription->subscriber_pid = subscriber_pid;
    subscription->active = 1;
    topic_copy(subscription->topic, topic);
    *event_value = event;
    return KU_STATUS_OK;
}

static ku_status_t unsubscribe(uint64_t subscriber_pid, const char* topic) {
    eventd_subscription* subscription = find_subscription(subscriber_pid, topic);
    ku_status_t close_status;
    if (subscription == (eventd_subscription*)0) return KU_STATUS_NOT_FOUND;
    close_status = ku_event_close(subscription->event);
    clear_subscription(subscription);
    return close_status;
}

static ku_status_t publish(const char* topic, uint64_t* signal_count) {
    size_t index = 0U;
    uint64_t count = 0U;
    ku_status_t result = KU_STATUS_OK;
    if (signal_count == (uint64_t*)0) return KU_STATUS_INVALID_ARGUMENT;
    for (; index < EVENTD_MAX_SUBSCRIPTIONS; ++index) {
        eventd_subscription* subscription = &subscriptions[index];
        ku_status_t status;
        if (!subscription->active || !topic_equal(subscription->topic, topic)) continue;
        status = ku_event_signal(subscription->event);
        if (status == KU_STATUS_OK) ++count;
        else if (result == KU_STATUS_OK) result = status;
    }
    *signal_count = count;
    return result;
}

static ku_status_t send_response(ku_service_connection_t connection, ku_status_t status, uint64_t value) {
    ku_event_broker_response response;
    response.structure_size = sizeof(response);
    response.status = status;
    response.value = value;
    return ku_service_send(connection, &response, sizeof(response));
}

static void handle_request(eventd_client* client, const ku_service_message* message) {
    const ku_event_broker_request* request;
    ku_status_t status = KU_STATUS_INVALID_ARGUMENT;
    uint64_t value = 0U;
    ku_status_t reply_status;

    if (client == (eventd_client*)0 || message == (const ku_service_message*)0) return;
    if (message->data_size != sizeof(ku_event_broker_request)) {
        (void)send_response(client->connection, KU_STATUS_CORRUPT_DATA, 0U);
        return;
    }
    request = (const ku_event_broker_request*)(const void*)message->data;
    if (request->structure_size != sizeof(*request) || !topic_valid(request->topic)) {
        (void)send_response(client->connection, KU_STATUS_INVALID_ARGUMENT, 0U);
        return;
    }
    if (client->pid == 0U) client->pid = message->sender_pid;
    else if (client->pid != message->sender_pid) {
        (void)send_response(client->connection, KU_STATUS_ACCESS_DENIED, 0U);
        return;
    }

    if (request->operation == KU_EVENT_BROKER_SUBSCRIBE)
        (void)u_puts("[TEST] event_broker_subscribe_receive: PASS\n");

    switch (request->operation) {
        case KU_EVENT_BROKER_SUBSCRIBE:
            status = subscribe(message->sender_pid, request->topic, &value);
            break;
        case KU_EVENT_BROKER_PUBLISH:
            status = publish(request->topic, &value);
            break;
        case KU_EVENT_BROKER_UNSUBSCRIBE:
            status = unsubscribe(message->sender_pid, request->topic);
            break;
        default:
            status = KU_STATUS_NOT_SUPPORTED;
            break;
    }

    reply_status = send_response(client->connection, status, value);
    if (request->operation == KU_EVENT_BROKER_SUBSCRIBE) {
        (void)u_puts(reply_status == KU_STATUS_OK
            ? "[TEST] event_broker_subscribe_reply: PASS\n"
            : "[TEST] event_broker_subscribe_reply: FAIL\n");
    }
}

static void report_accept_error(ku_result_t accepted) {
    if (accepted == KU_STATUS_ACCESS_DENIED)
        (void)u_puts("[TEST] event_broker_accept_error: ACCESS_DENIED\n");
    else if (accepted == KU_STATUS_INVALID_ARGUMENT)
        (void)u_puts("[TEST] event_broker_accept_error: INVALID_ARGUMENT\n");
    else if (accepted == KU_STATUS_NOT_FOUND)
        (void)u_puts("[TEST] event_broker_accept_error: NOT_FOUND\n");
    else
        (void)u_puts("[TEST] event_broker_accept_error: OTHER\n");
}

static void accept_clients(ku_service_endpoint_t endpoint) {
    for (;;) {
        ku_result_t accepted = ku_service_accept(endpoint);
        size_t index = 0U;
        if (accepted == KU_STATUS_WOULD_BLOCK) {
            if (!accept_wait_reported) {
                accept_wait_reported = 1;
                (void)u_puts("[TEST] event_broker_accept_wait: PASS\n");
            }
            return;
        }
        if (accepted <= 0) {
            report_accept_error(accepted);
            return;
        }
        (void)u_puts("[TEST] event_broker_accept: PASS\n");
        for (; index < EVENTD_MAX_CLIENTS; ++index) {
            if (!clients[index].active) {
                clients[index].connection = (ku_service_connection_t)accepted;
                clients[index].pid = 0U;
                clients[index].active = 1;
                break;
            }
        }
        if (index == EVENTD_MAX_CLIENTS) {
            (void)ku_service_close((ku_service_connection_t)accepted);
            return;
        }
    }
}

static void service_clients(void) {
    size_t index = 0U;
    for (; index < EVENTD_MAX_CLIENTS; ++index) {
        eventd_client* client = &clients[index];
        ku_service_message message;
        ku_status_t status;
        if (!client->active) continue;
        status = ku_service_receive(client->connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) continue;
        if (status != KU_STATUS_OK) {
            cleanup_subscriber(client->pid);
            (void)ku_service_close(client->connection);
            client->connection = 0U;
            client->pid = 0U;
            client->active = 0;
            continue;
        }
        handle_request(client, &message);
    }
}

__attribute__((noreturn)) void _start(void) {
    ku_result_t endpoint = ku_service_register(
        KU_EVENT_BROKER_SERVICE_NAME,
        KU_EVENT_BROKER_SERVICE_NAME_SIZE);
    if (endpoint <= 0) {
        (void)u_puts("eventd: service registration failed\n");
        (void)u_puts("[TEST] event_broker_service: FAIL\n");
        ku_exit(1);
    }

    (void)u_puts("eventd: events.v1 online\n");
    (void)u_puts("[TEST] event_broker_service: PASS\n");
    for (;;) {
        accept_clients((ku_service_endpoint_t)endpoint);
        service_clients();
        (void)ku_sleep(1U);
        if (!loop_resume_reported) {
            loop_resume_reported = 1;
            (void)u_puts("[TEST] event_broker_loop_resumed: PASS\n");
        }
    }
}
