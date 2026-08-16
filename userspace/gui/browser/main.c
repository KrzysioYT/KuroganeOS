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
static uint32_t g_http_status = 0U;

static void append_text(char* destination, size_t capacity, const char* source) {
    const size_t used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
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
        char number[24];
        gui_u64(number, sizeof(number), (uint64_t)(-(int64_t)result));
        (void)strlcpy(g_status, "HTTP FAILED / STATUS -", sizeof(g_status));
        append_text(g_status, sizeof(g_status), number);
        memset(g_lines, 0, sizeof(g_lines));
        (void)strlcpy(g_lines[0], "CHECK NAT / E1000 / DHCP / DNS", VIEW_LINE_CAPACITY);
        return;
    }

    if (request.bytes_received >= sizeof(g_page)) {
        request.bytes_received = sizeof(g_page) - 1U;
    }
    g_page[(size_t)request.bytes_received] = '\0';
    g_http_status = request.http_status;
    render_text_body(find_body(g_page));
    (void)strlcpy(g_status, "LOADED / HTTP ", sizeof(g_status));
    {
        char number[24];
        gui_u64(number, sizeof(number), g_http_status);
        append_text(g_status, sizeof(g_status), number);
    }
}

static void build_scene(kui_scene* scene) {
    kui_flow root;
    size_t index;
    char address[64] = "URL  ";

    append_text(address, sizeof(address), g_url);
    kui_scene_initialize(scene);
    scene->visible_rows = 12U;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        UINT32_C(0xDE192D));

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "KUROGANE WEB / NATIVE HTTP");
    (void)kui_flow_input(&root, 2U, address);
    (void)kui_flow_label(&root, 3U, "ENTER: LOAD   ESC: CLEAR   HTTP/1.x DEV BETA");
    (void)kui_flow_label(&root, 4U, g_status);
    (void)kui_flow_separator(&root, 5U);
    for (index = 0U; index < VIEW_LINES; ++index) {
        (void)kui_flow_label(&root, 10U + (uint32_t)index,
            g_lines[index][0] != '\0' ? g_lines[index] : " ");
    }
    (void)kui_flow_label(&root, 20U,
        "CHROMIUM/TLS: FUTURE PLATFORM LAYER - NOT FAKED");
}

int main(void) {
    const ku_window_t window = gui_open("KUROGANE WEB", 155, 115, 720, 470);
    kui_scene scene;
    if (window == KU_INVALID_WINDOW) return 1;

    puts("[TEST] desktop_browser_ui: PASS");
    memset(g_lines, 0, sizeof(g_lines));
    (void)strlcpy(g_lines[0], "TYPE AN http:// ADDRESS AND PRESS ENTER", VIEW_LINE_CAPACITY);

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
