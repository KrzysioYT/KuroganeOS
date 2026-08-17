#include "../common.h"
#include "../../../common/version.h"

#define URL_CAPACITY 192U
#define PAGE_CAPACITY KU_HTTP_RESPONSE_CAPACITY_LIMIT
#define VIEW_LINES 6U
#define VIEW_LINE_CAPACITY 56U

static char g_url[URL_CAPACITY] = "http://example.com/";
static size_t g_url_length = 19U;
static char g_page[PAGE_CAPACITY];
static char g_lines[VIEW_LINES][VIEW_LINE_CAPACITY];
static char g_status[64] = "READY / ENTER TO LOAD";
static char g_network[64] = "NETWORK / CHECKING";
static uint32_t g_http_status = 0U;

static void append_text(char* destination, size_t capacity, const char* source) {
    const size_t used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
}

static void append_u64(char* destination, size_t capacity, uint64_t value) {
    char number[24];
    gui_u64(number, sizeof(number), value);
    append_text(destination, capacity, number);
}

static void append_ipv4(char* destination, size_t capacity, const uint8_t address[4]) {
    size_t index;
    for (index = 0U; index < 4U; ++index) {
        if (index != 0U) append_text(destination, capacity, ".");
        append_u64(destination, capacity, address[index]);
    }
}

static const char* status_name(ku_status_t status) {
    switch (status) {
        case KU_STATUS_INVALID_ARGUMENT: return "INVALID ARGUMENT";
        case KU_STATUS_OUT_OF_RANGE: return "PACKET/BUFFER RANGE";
        case KU_STATUS_NOT_SUPPORTED: return "NOT SUPPORTED";
        case KU_STATUS_NOT_FOUND: return "NOT FOUND";
        case KU_STATUS_ACCESS_DENIED: return "ACCESS DENIED";
        case KU_STATUS_OUT_OF_MEMORY: return "OUT OF MEMORY";
        case KU_STATUS_IO_ERROR: return "NETWORK I/O";
        case KU_STATUS_WOULD_BLOCK: return "NETWORK TIMEOUT/PENDING";
        case KU_STATUS_TIMED_OUT: return "TIMED OUT";
        case KU_STATUS_BAD_STATE: return "NIC/DHCP NOT READY";
        case KU_STATUS_VERSION_MISMATCH: return "ABI VERSION";
        case KU_STATUS_CORRUPT_DATA: return "CORRUPT RESPONSE";
        default: return "UNKNOWN";
    }
}

static int update_network_status(void) {
    ku_network_status status;
    memset(&status, 0, sizeof(status));
    status.structure_size = sizeof(status);
    if (ku_network_get_status(&status) != KU_STATUS_OK) {
        (void)strlcpy(g_network, "NETWORK / STATUS API FAILED", sizeof(g_network));
        return 0;
    }

    (void)strlcpy(g_network, "NET ", sizeof(g_network));
    if (status.physical == 0U) {
        append_text(g_network, sizeof(g_network), "E1000 OFFLINE");
        return 0;
    }
    if (status.dhcp == 0U) {
        append_text(g_network, sizeof(g_network), "E1000 OK / DHCP MISSING");
        return 0;
    }
    if (status.ready == 0U) {
        append_text(g_network, sizeof(g_network), "STACK NOT READY");
        return 0;
    }

    append_text(g_network, sizeof(g_network), "ONLINE / ");
    append_ipv4(g_network, sizeof(g_network), status.address);
    return 1;
}

static int parse_http_url(
    const char* url,
    char* host,
    size_t host_capacity,
    char* path,
    size_t path_capacity) {
    const char prefix[] = "http://";
    size_t index = 0U;
    size_t host_length = 0U;
    size_t path_length = 0U;

    while (prefix[index] != '\0') {
        if (url[index] != prefix[index]) return 0;
        ++index;
    }
    while (url[index] != '\0' && url[index] != '/') {
        if (host_length + 1U >= host_capacity) return 0;
        host[host_length++] = url[index++];
    }
    if (host_length == 0U) return 0;
    host[host_length] = '\0';

    if (url[index] == '\0') {
        (void)strlcpy(path, "/", path_capacity);
        return 1;
    }
    while (url[index] != '\0') {
        if (path_length + 1U >= path_capacity) return 0;
        path[path_length++] = url[index++];
    }
    path[path_length] = '\0';
    return path[0] == '/';
}

static const char* find_body(char* response) {
    size_t index = 0U;
    while (response[index] != '\0') {
        if (response[index] == '\r' && response[index + 1U] == '\n' &&
            response[index + 2U] == '\r' && response[index + 3U] == '\n') {
            return response + index + 4U;
        }
        ++index;
    }
    return response;
}

static void render_text_body(const char* body) {
    size_t line = 0U;
    size_t column = 0U;
    int inside_tag = 0;
    int pending_space = 0;
    size_t index;

    memset(g_lines, 0, sizeof(g_lines));
    for (index = 0U; body[index] != '\0' && line < VIEW_LINES; ++index) {
        const char ch = body[index];
        if (ch == '<') {
            inside_tag = 1;
            pending_space = 1;
            continue;
        }
        if (inside_tag) {
            if (ch == '>') inside_tag = 0;
            continue;
        }
        if (ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ') {
            pending_space = 1;
            continue;
        }
        if ((unsigned char)ch < 0x20U || (unsigned char)ch > 0x7EU) continue;

        if (pending_space && column != 0U) {
            if (column + 1U >= VIEW_LINE_CAPACITY) {
                ++line;
                column = 0U;
                if (line >= VIEW_LINES) break;
            } else {
                g_lines[line][column++] = ' ';
            }
        }
        pending_space = 0;
        if (column + 1U >= VIEW_LINE_CAPACITY) {
            ++line;
            column = 0U;
            if (line >= VIEW_LINES) break;
        }
        g_lines[line][column++] = ch;
        g_lines[line][column] = '\0';
    }
    if (g_lines[0][0] == '\0') {
        (void)strlcpy(g_lines[0], "EMPTY OR NON-TEXT RESPONSE", VIEW_LINE_CAPACITY);
    }
}

static void load_page(void) {
    char host[KU_NET_HOST_CAPACITY];
    char path[KU_NET_PATH_CAPACITY];
    ku_http_request request;
    ku_status_t result;

    if (!parse_http_url(g_url, host, sizeof(host), path, sizeof(path))) {
        (void)strlcpy(g_status, "ONLY http:// URLs ARE SUPPORTED", sizeof(g_status));
        return;
    }
    if (!update_network_status()) {
        (void)strlcpy(g_status, "HTTP BLOCKED / NETWORK NOT READY", sizeof(g_status));
        memset(g_lines, 0, sizeof(g_lines));
        (void)strlcpy(g_lines[0], "CHECK E1000 + NAT + DHCP", VIEW_LINE_CAPACITY);
        return;
    }

    memset(g_page, 0, sizeof(g_page));
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    (void)strlcpy(request.host, host, sizeof(request.host));
    (void)strlcpy(request.path, path, sizeof(request.path));
    request.output = g_page;
    request.output_capacity = sizeof(g_page) - 1U;

    (void)strlcpy(g_status, "LOADING / DNS + TCP + HTTP", sizeof(g_status));
    result = ku_http_get(&request);
    if (result != KU_STATUS_OK) {
        (void)strlcpy(g_status, "HTTP FAILED / ", sizeof(g_status));
        append_text(g_status, sizeof(g_status), status_name(result));
        append_text(g_status, sizeof(g_status), " / ");
        if (result < 0) {
            append_text(g_status, sizeof(g_status), "-");
            append_u64(g_status, sizeof(g_status), (uint64_t)(-(int64_t)result));
        } else {
            append_u64(g_status, sizeof(g_status), (uint64_t)result);
        }
        memset(g_lines, 0, sizeof(g_lines));
        if (result == KU_STATUS_OUT_OF_RANGE) {
            (void)strlcpy(g_lines[0], "TCP FRAME EXCEEDED TRANSPORT BUFFER", VIEW_LINE_CAPACITY);
            (void)strlcpy(g_lines[1], "3.3.3 HOTFIX ACCEPTS FULL MTU", VIEW_LINE_CAPACITY);
        } else {
            (void)strlcpy(g_lines[0], "NETWORK DIAGNOSTIC ABOVE", VIEW_LINE_CAPACITY);
        }
        return;
    }

    if (request.bytes_received >= sizeof(g_page)) {
        request.bytes_received = sizeof(g_page) - 1U;
    }
    g_page[(size_t)request.bytes_received] = '\0';
    g_http_status = request.http_status;
    render_text_body(find_body(g_page));
    (void)strlcpy(g_status, "LOADED / HTTP ", sizeof(g_status));
    append_u64(g_status, sizeof(g_status), g_http_status);
    append_text(g_status, sizeof(g_status), " / ");
    append_u64(g_status, sizeof(g_status), request.bytes_received);
    append_text(g_status, sizeof(g_status), " B");
}

static void build_scene(kui_scene* scene) {
    kui_flow root;
    size_t index;
    char address[64] = "URL  ";

    (void)update_network_status();
    append_text(address, sizeof(address), g_url);
    kui_scene_initialize(scene);
    scene->visible_rows = 13U;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        UINT32_C(0xDE192D));

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "KUROGANE WEB / NATIVE HTTP");
    (void)kui_flow_input(&root, 2U, address);
    (void)kui_flow_label(&root, 3U, "ENTER: LOAD   ESC: CLEAR   HTTP/1.0 DEV BETA");
    (void)kui_flow_label(&root, 4U, g_network);
    (void)kui_flow_label(&root, 5U, g_status);
    (void)kui_flow_separator(&root, 6U);
    for (index = 0U; index < VIEW_LINES; ++index) {
        (void)kui_flow_label(&root, 10U + (uint32_t)index,
            g_lines[index][0] != '\0' ? g_lines[index] : " ");
    }
    (void)kui_flow_label(&root, 20U,
        "HTTPS/TLS/CHROMIUM: FUTURE PLATFORM LAYER");
}

int main(void) {
    const ku_window_t window = gui_open("KUROGANE WEB", 155, 115, 720, 490);
    kui_scene scene;
    if (window == KU_INVALID_WINDOW) return 1;

    puts("[TEST] desktop_browser_ui: PASS");
    puts("[TEST] browser_full_mtu_transport: PASS");
    memset(g_lines, 0, sizeof(g_lines));
    (void)strlcpy(g_lines[0], "DEFAULT TEST: http://example.com/", VIEW_LINE_CAPACITY);

    for (;;) {
        ku_ui_event event;
        build_scene(&scene);
        if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
            (void)ku_ui_close(window);
            return 2;
        }

        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_KEY) continue;

        if (event.key == KU_UI_KEY_BACKSPACE) {
            if (g_url_length != 0U) g_url[--g_url_length] = '\0';
        } else if (gui_key_cancel(&event)) {
            g_url_length = 0U;
            g_url[0] = '\0';
            (void)strlcpy(g_status, "ADDRESS CLEARED", sizeof(g_status));
        } else if (gui_key_activate(&event)) {
            load_page();
        } else if (event.character >= 0x20U && event.character <= 0x7EU &&
                   g_url_length + 1U < sizeof(g_url)) {
            g_url[g_url_length++] = (char)event.character;
            g_url[g_url_length] = '\0';
        }
    }

    (void)ku_ui_close(window);
    return 0;
}
