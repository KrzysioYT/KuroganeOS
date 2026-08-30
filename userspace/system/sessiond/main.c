#include "../../runtime/user.h"

#include <kurogane/session.h>

#define SESSIOND_MAX_CLIENTS 8U
#define SESSIOND_MAX_SESSIONS 4U

typedef struct sessiond_client {
    ku_service_connection_t connection;
    uint64_t pid;
    int active;
} sessiond_client;

typedef struct sessiond_session {
    ku_session_id_t id;
    uint64_t account_id;
    uint64_t owner_pid;
    uint64_t home_pid;
    uint64_t applications[KU_SESSION_MAX_APPLICATIONS];
    uint32_t application_count;
    uint32_t state;
    int active;
} sessiond_session;

static sessiond_client clients[SESSIOND_MAX_CLIENTS];
static sessiond_session sessions[SESSIOND_MAX_SESSIONS];
static ku_session_id_t next_session_id = UINT64_C(1);

static void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static ku_session_id_t allocate_session_id(void) {
    ku_session_id_t value = next_session_id++;
    if (value == KU_SESSION_INVALID_ID) value = next_session_id++;
    if (next_session_id == KU_SESSION_INVALID_ID) next_session_id = UINT64_C(1);
    return value;
}

static sessiond_session* find_session(ku_session_id_t id) {
    size_t index;
    if (id == KU_SESSION_INVALID_ID) return (sessiond_session*)0;
    for (index = 0U; index < SESSIOND_MAX_SESSIONS; ++index) {
        if (sessions[index].active && sessions[index].id == id) return &sessions[index];
    }
    return (sessiond_session*)0;
}

static sessiond_session* reserve_session(void) {
    size_t index;
    for (index = 0U; index < SESSIOND_MAX_SESSIONS; ++index) {
        if (!sessions[index].active) return &sessions[index];
    }
    return (sessiond_session*)0;
}

static int application_index(const sessiond_session* session, uint64_t pid) {
    uint32_t index;
    if (session == (const sessiond_session*)0 || pid == 0U) return -1;
    for (index = 0U; index < session->application_count; ++index) {
        if (session->applications[index] == pid) return (int)index;
    }
    return -1;
}

static void fill_response(
    ku_session_response* response,
    ku_status_t status,
    const sessiond_session* session) {
    uint32_t index;
    clear_bytes(response, sizeof(*response));
    response->structure_size = sizeof(*response);
    response->status = status;
    if (status != KU_STATUS_OK || session == (const sessiond_session*)0) return;
    response->session_id = session->id;
    response->account_id = session->account_id;
    response->owner_pid = session->owner_pid;
    response->home_pid = session->home_pid;
    response->state = session->state;
    response->application_count = session->application_count;
    for (index = 0U; index < session->application_count; ++index)
        response->applications[index] = session->applications[index];
}

static ku_status_t send_response(
    ku_service_connection_t connection,
    ku_status_t status,
    const sessiond_session* session) {
    ku_session_response response;
    fill_response(&response, status, session);
    return ku_service_send(connection, &response, sizeof(response));
}

static void release_owned_sessions(uint64_t owner_pid) {
    size_t index;
    if (owner_pid == 0U) return;
    for (index = 0U; index < SESSIOND_MAX_SESSIONS; ++index) {
        if (sessions[index].active && sessions[index].owner_pid == owner_pid)
            clear_bytes(&sessions[index], sizeof(sessions[index]));
    }
}

static void handle_request(
    sessiond_client* client,
    const ku_service_message* message) {
    const ku_session_request* request;
    sessiond_session* session;
    int index;

    if (client == (sessiond_client*)0 || message == (const ku_service_message*)0) return;
    if (message->data_size != sizeof(ku_session_request)) {
        (void)send_response(client->connection, KU_STATUS_CORRUPT_DATA, 0);
        return;
    }
    request = (const ku_session_request*)(const void*)message->data;
    if (request->structure_size != sizeof(*request) || request->reserved != 0U) {
        (void)send_response(client->connection, KU_STATUS_INVALID_ARGUMENT, 0);
        return;
    }
    if (client->pid == 0U) client->pid = message->sender_pid;
    else if (client->pid != message->sender_pid) {
        (void)send_response(client->connection, KU_STATUS_ACCESS_DENIED, 0);
        return;
    }

    if (request->operation == KU_SESSION_CREATE) {
        if (request->session_id != KU_SESSION_INVALID_ID || request->account_id == 0U ||
            request->process_id != 0U) {
            (void)send_response(client->connection, KU_STATUS_INVALID_ARGUMENT, 0);
            return;
        }
        for (index = 0; index < (int)SESSIOND_MAX_SESSIONS; ++index) {
            if (sessions[index].active && sessions[index].owner_pid == client->pid) {
                (void)send_response(client->connection, KU_STATUS_ALREADY_EXISTS, 0);
                return;
            }
        }
        session = reserve_session();
        if (session == (sessiond_session*)0) {
            (void)send_response(client->connection, KU_STATUS_OUT_OF_MEMORY, 0);
            return;
        }
        clear_bytes(session, sizeof(*session));
        session->id = allocate_session_id();
        session->account_id = request->account_id;
        session->owner_pid = client->pid;
        session->state = KU_SESSION_STATE_ACTIVE;
        session->active = 1;
        (void)send_response(client->connection, KU_STATUS_OK, session);
        return;
    }

    session = find_session(request->session_id);
    if (session == (sessiond_session*)0) {
        (void)send_response(client->connection, KU_STATUS_NOT_FOUND, 0);
        return;
    }
    if (session->owner_pid != client->pid) {
        (void)send_response(client->connection, KU_STATUS_ACCESS_DENIED, 0);
        return;
    }
    if (request->account_id != 0U && request->account_id != session->account_id) {
        (void)send_response(client->connection, KU_STATUS_INVALID_ARGUMENT, 0);
        return;
    }

    switch (request->operation) {
        case KU_SESSION_QUERY:
            if (request->process_id != 0U) {
                (void)send_response(client->connection, KU_STATUS_INVALID_ARGUMENT, 0);
                return;
            }
            (void)send_response(client->connection, KU_STATUS_OK, session);
            return;

        case KU_SESSION_SET_HOME:
            if (request->process_id == 0U) {
                (void)send_response(client->connection, KU_STATUS_INVALID_ARGUMENT, 0);
                return;
            }
            session->home_pid = request->process_id;
            (void)send_response(client->connection, KU_STATUS_OK, session);
            return;

        case KU_SESSION_ATTACH_APPLICATION:
            if (request->process_id == 0U) {
                (void)send_response(client->connection, KU_STATUS_INVALID_ARGUMENT, 0);
                return;
            }
            if (application_index(session, request->process_id) >= 0) {
                (void)send_response(client->connection, KU_STATUS_ALREADY_EXISTS, 0);
                return;
            }
            if (session->application_count >= KU_SESSION_MAX_APPLICATIONS) {
                (void)send_response(client->connection, KU_STATUS_OUT_OF_MEMORY, 0);
                return;
            }
            session->applications[session->application_count++] = request->process_id;
            (void)send_response(client->connection, KU_STATUS_OK, session);
            return;

        case KU_SESSION_DETACH_APPLICATION:
            index = application_index(session, request->process_id);
            if (index < 0) {
                (void)send_response(client->connection, KU_STATUS_NOT_FOUND, 0);
                return;
            }
            while ((uint32_t)index + 1U < session->application_count) {
                session->applications[index] = session->applications[index + 1];
                ++index;
            }
            --session->application_count;
            session->applications[session->application_count] = 0U;
            if (session->home_pid == request->process_id) session->home_pid = 0U;
            (void)send_response(client->connection, KU_STATUS_OK, session);
            return;

        case KU_SESSION_TERMINATE: {
            ku_session_response response;
            if (request->process_id != 0U) {
                (void)send_response(client->connection, KU_STATUS_INVALID_ARGUMENT, 0);
                return;
            }
            session->state = KU_SESSION_STATE_TERMINATING;
            fill_response(&response, KU_STATUS_OK, session);
            clear_bytes(session, sizeof(*session));
            (void)ku_service_send(client->connection, &response, sizeof(response));
            return;
        }

        default:
            (void)send_response(client->connection, KU_STATUS_NOT_SUPPORTED, 0);
            return;
    }
}

static void accept_clients(ku_service_endpoint_t endpoint) {
    for (;;) {
        const ku_result_t accepted = ku_service_accept(endpoint);
        size_t index;
        if (accepted == KU_STATUS_WOULD_BLOCK) return;
        if (accepted <= 0) return;
        for (index = 0U; index < SESSIOND_MAX_CLIENTS; ++index) {
            if (!clients[index].active) {
                clients[index].connection = (ku_service_connection_t)accepted;
                clients[index].pid = 0U;
                clients[index].active = 1;
                break;
            }
        }
        if (index == SESSIOND_MAX_CLIENTS) {
            (void)ku_service_close((ku_service_connection_t)accepted);
            return;
        }
    }
}

static void service_clients(void) {
    size_t index;
    for (index = 0U; index < SESSIOND_MAX_CLIENTS; ++index) {
        sessiond_client* client = &clients[index];
        ku_service_message message;
        ku_status_t status;
        if (!client->active) continue;
        status = ku_service_receive(client->connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) continue;
        if (status != KU_STATUS_OK) {
            release_owned_sessions(client->pid);
            (void)ku_service_close(client->connection);
            clear_bytes(client, sizeof(*client));
            continue;
        }
        handle_request(client, &message);
    }
}

__attribute__((noreturn)) void _start(void) {
    const ku_result_t endpoint = ku_service_register(
        KU_SESSION_SERVICE_NAME, KU_SESSION_SERVICE_NAME_SIZE);
    if (endpoint <= 0) {
        (void)u_puts("sessiond: service registration failed\n");
        (void)u_puts("[TEST] session_service: FAIL\n");
        ku_exit(1);
    }
    (void)u_puts("sessiond: session.v1 online\n");
    (void)u_puts("[TEST] session_service: PASS\n");
    for (;;) {
        accept_clients((ku_service_endpoint_t)endpoint);
        service_clients();
        (void)ku_sleep(1U);
    }
}
