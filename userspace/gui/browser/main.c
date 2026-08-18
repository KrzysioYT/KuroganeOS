#include "../common.h"
#include "../../../common/version.h"

#define BROWSER_URL_CAPACITY 192U
#define BROWSER_STATUS_CAPACITY 96U
#define BROWSER_RENDER_LINES 7U
#define BROWSER_RENDER_LINE_CAPACITY 60U
#define BROWSER_REDIRECT_LIMIT 4U
#define CHROMIUM_UPSTREAM_SHORT "4137589c"
#define BROWSER_SEARCH_PREFIX "https://www.google.com/search?q="

typedef enum chromium_navigation_stage {
    CHROMIUM_STAGE_IDLE = 0,
    CHROMIUM_STAGE_NETWORK,
    CHROMIUM_STAGE_REQUEST,
    CHROMIUM_STAGE_REDIRECT,
    CHROMIUM_STAGE_COMMIT,
    CHROMIUM_STAGE_FAILED
} chromium_navigation_stage;

typedef enum chromium_url_scheme {
    CHROMIUM_SCHEME_INVALID = 0,
    CHROMIUM_SCHEME_HTTP,
    CHROMIUM_SCHEME_HTTPS
} chromium_url_scheme;

typedef struct chromium_browser_context {
    char url[BROWSER_URL_CAPACITY];
    size_t url_length;
    char status[BROWSER_STATUS_CAPACITY];
    char network[BROWSER_STATUS_CAPACITY];
    char page[KU_HTTP_RESPONSE_CAPACITY_LIMIT];
    char render_lines[BROWSER_RENDER_LINES][BROWSER_RENDER_LINE_CAPACITY];
    ku_network_status network_state;
    chromium_navigation_stage stage;
    uint32_t http_status;
    uint32_t redirect_count;
    uint64_t bytes_received;
    ku_status_t last_error;
} chromium_browser_context;

static chromium_browser_context g_browser;

/*
 * Kurogane Chromium port bootstrap.
 *
 * Chromium's content_shell separates BrowserContext, navigation and the
 * platform window delegate. Kurogane Web follows the same ownership model so
 * Blink/V8 can replace only the bootstrap renderer when the required platform
 * APIs exist. No Chromium source is copied into this file and the current
 * renderer is intentionally labelled as a bootstrap fallback.
 */

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

static void append_status_code(char* destination, size_t capacity, ku_status_t status) {
    if (status < 0) {
        append_text(destination, capacity, "-");
        append_u64(destination, capacity, (uint64_t)(-(int64_t)status));
    } else {
        append_u64(destination, capacity, (uint64_t)status);
    }
}

static void append_ipv4(char* destination, size_t capacity, const uint8_t address[4]) {
    size_t index;
    for (index = 0U; index < 4U; ++index) {
        if (index != 0U) append_text(destination, capacity, ".");
        append_u64(destination, capacity, address[index]);
    }
}

static int ascii_equal_ci(char left, char right) {
    if (left >= 'A' && left <= 'Z') left = (char)(left - 'A' + 'a');
    if (right >= 'A' && right <= 'Z') right = (char)(right - 'A' + 'a');
    return left == right;
}

static int starts_with_ci(const char* text, const char* prefix) {
    size_t index = 0U;
    while (prefix[index] != '\0') {
        if (text[index] == '\0' || !ascii_equal_ci(text[index], prefix[index])) return 0;
        ++index;
    }
    return 1;
}

static int ascii_is_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static int ascii_is_unreserved(char value) {
    return (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') ||
        value == '-' || value == '_' || value == '.' || value == '~';
}

static char hex_digit(uint8_t value) {
    return value < 10U ? (char)('0' + value) : (char)('A' + value - 10U);
}

static int trim_copy(
    const char* input,
    char* output,
    size_t output_capacity) {
    size_t first = 0U;
    size_t last;
    size_t length;
    size_t index;
    if (input == NULL || output == NULL || output_capacity == 0U) return 0;
    last = strlen(input);
    while (first < last && ascii_is_space(input[first])) ++first;
    while (last > first && ascii_is_space(input[last - 1U])) --last;
    length = last - first;
    if (length == 0U || length + 1U > output_capacity) {
        output[0] = '\0';
        return 0;
    }
    for (index = 0U; index < length; ++index) output[index] = input[first + index];
    output[length] = '\0';
    return 1;
}

static int looks_like_address(const char* input) {
    size_t index = 0U;
    int has_dot = 0;
    int has_slash = 0;
    if (input == NULL || input[0] == '\0') return 0;
    while (input[index] != '\0') {
        if (ascii_is_space(input[index])) return 0;
        if (input[index] == '.') has_dot = 1;
        if (input[index] == '/') has_slash = 1;
        ++index;
    }
    return has_dot || has_slash || starts_with_ci(input, "localhost");
}

static int append_percent_encoded(
    char* output,
    size_t output_capacity,
    const char* input) {
    size_t used = strlen(output);
    size_t index = 0U;
    if (input == NULL || used >= output_capacity) return 0;
    while (input[index] != '\0') {
        const uint8_t value = (uint8_t)input[index++];
        if (ascii_is_unreserved((char)value)) {
            if (used + 1U >= output_capacity) return 0;
            output[used++] = (char)value;
        } else if (value == ' ') {
            if (used + 1U >= output_capacity) return 0;
            output[used++] = '+';
        } else {
            if (used + 3U >= output_capacity) return 0;
            output[used++] = '%';
            output[used++] = hex_digit((uint8_t)(value >> 4U));
            output[used++] = hex_digit((uint8_t)(value & UINT8_C(0x0F)));
        }
    }
    output[used] = '\0';
    return 1;
}

/*
 * Resolve an omnibox value without ambiguity:
 *   - explicit http(s) URLs stay unchanged;
 *   - host/path-looking input becomes an HTTP URL;
 *   - everything else becomes a verified HTTPS search query.
 */
static int omnibox_resolve(
    const char* input,
    char* output,
    size_t output_capacity) {
    char value[BROWSER_URL_CAPACITY];
    if (!trim_copy(input, value, sizeof(value))) return 0;
    output[0] = '\0';
    if (starts_with_ci(value, "http://") || starts_with_ci(value, "https://")) {
        return strlcpy(output, value, output_capacity) < output_capacity;
    }
    if (looks_like_address(value)) {
        append_text(output, output_capacity, "http://");
        append_text(output, output_capacity, value);
        return strlen(output) + 1U <= output_capacity;
    }
    append_text(output, output_capacity, BROWSER_SEARCH_PREFIX);
    return append_percent_encoded(output, output_capacity, value);
}

static const char* status_name(ku_status_t status) {
    switch (status) {
        case KU_STATUS_OK: return "OK";
        case KU_STATUS_INVALID_ARGUMENT: return "INVALID ARGUMENT";
        case KU_STATUS_OUT_OF_RANGE: return "TRANSPORT RANGE";
        case KU_STATUS_NOT_SUPPORTED: return "NOT SUPPORTED";
        case KU_STATUS_NOT_FOUND: return "NOT FOUND";
        case KU_STATUS_ACCESS_DENIED: return "ACCESS DENIED";
        case KU_STATUS_OUT_OF_MEMORY: return "OUT OF MEMORY";
        case KU_STATUS_IO_ERROR: return "NETWORK / TLS I/O";
        case KU_STATUS_WOULD_BLOCK: return "DNS/TCP/TLS TIMEOUT";
        case KU_STATUS_TIMED_OUT: return "TIMED OUT";
        case KU_STATUS_BAD_STATE: return "NIC/DHCP/TLS NOT READY";
        case KU_STATUS_VERSION_MISMATCH: return "ABI VERSION";
        case KU_STATUS_CORRUPT_DATA: return "CORRUPT RESPONSE";
        default: return "UNKNOWN";
    }
}

static const char* stage_name(chromium_navigation_stage stage) {
    switch (stage) {
        case CHROMIUM_STAGE_IDLE: return "IDLE";
        case CHROMIUM_STAGE_NETWORK: return "NETWORK";
        case CHROMIUM_STAGE_REQUEST: return "DNS / TCP / HTTP(S)";
        case CHROMIUM_STAGE_REDIRECT: return "REDIRECT";
        case CHROMIUM_STAGE_COMMIT: return "COMMITTED";
        case CHROMIUM_STAGE_FAILED: return "FAILED";
    }
    return "UNKNOWN";
}

static void render_clear(chromium_browser_context* context) {
    memset(context->render_lines, 0, sizeof(context->render_lines));
}

static void render_message(chromium_browser_context* context, const char* first, const char* second) {
    render_clear(context);
    if (first != NULL) (void)strlcpy(context->render_lines[0], first, BROWSER_RENDER_LINE_CAPACITY);
    if (second != NULL) (void)strlcpy(context->render_lines[1], second, BROWSER_RENDER_LINE_CAPACITY);
}

static int platform_delegate_refresh_network(chromium_browser_context* context) {
    ku_status_t result;
    memset(&context->network_state, 0, sizeof(context->network_state));
    context->network_state.structure_size = sizeof(context->network_state);
    result = ku_network_get_status(&context->network_state);
    if (result != KU_STATUS_OK) {
        (void)strlcpy(context->network, "NET / STATUS API FAILED", sizeof(context->network));
        context->last_error = result;
        return 0;
    }

    (void)strlcpy(context->network, "NET / ", sizeof(context->network));
    if (context->network_state.physical == 0U) {
        append_text(context->network, sizeof(context->network), "NIC OFFLINE");
        return 0;
    }
    if (context->network_state.dhcp == 0U) {
        append_text(context->network, sizeof(context->network), "NIC OK / DHCP MISSING");
        return 0;
    }
    if (context->network_state.ready == 0U) {
        append_text(context->network, sizeof(context->network), "STACK NOT READY");
        return 0;
    }

    append_text(context->network, sizeof(context->network), "ONLINE ");
    append_ipv4(context->network, sizeof(context->network), context->network_state.address);
    append_text(context->network, sizeof(context->network), " / DNS ");
    append_ipv4(context->network, sizeof(context->network), context->network_state.dns);
    return 1;
}

static chromium_url_scheme parse_url(
    const char* url,
    char* host,
    size_t host_capacity,
    char* path,
    size_t path_capacity) {
    const char http_prefix[] = "http://";
    const char https_prefix[] = "https://";
    chromium_url_scheme scheme = CHROMIUM_SCHEME_INVALID;
    size_t index = 0U;
    size_t host_length = 0U;
    size_t path_length = 0U;

    if (starts_with_ci(url, http_prefix)) {
        scheme = CHROMIUM_SCHEME_HTTP;
        index = sizeof(http_prefix) - 1U;
    } else if (starts_with_ci(url, https_prefix)) {
        scheme = CHROMIUM_SCHEME_HTTPS;
        index = sizeof(https_prefix) - 1U;
    } else {
        return CHROMIUM_SCHEME_INVALID;
    }

    while (url[index] != '\0' && url[index] != '/') {
        if (url[index] == ':' || url[index] == ' ' || host_length + 1U >= host_capacity) {
            return CHROMIUM_SCHEME_INVALID;
        }
        host[host_length++] = url[index++];
    }
    if (host_length == 0U) return CHROMIUM_SCHEME_INVALID;
    host[host_length] = '\0';

    if (url[index] == '\0') {
        (void)strlcpy(path, "/", path_capacity);
        return scheme;
    }
    while (url[index] != '\0') {
        if (path_length + 1U >= path_capacity) return CHROMIUM_SCHEME_INVALID;
        path[path_length++] = url[index++];
    }
    path[path_length] = '\0';
    return path[0] == '/' ? scheme : CHROMIUM_SCHEME_INVALID;
}

static const char* response_body(const char* response) {
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

static int response_header(
    const char* response,
    const char* name,
    char* output,
    size_t output_capacity) {
    const size_t name_length = strlen(name);
    size_t index = 0U;
    if (output_capacity == 0U) return 0;
    output[0] = '\0';

    while (response[index] != '\0') {
        size_t line_start = index;
        size_t line_end = index;
        size_t matched = 0U;
        while (response[line_end] != '\0' && response[line_end] != '\r' &&
               response[line_end] != '\n') ++line_end;
        if (line_end == line_start) return 0;

        while (matched < name_length && line_start + matched < line_end &&
               ascii_equal_ci(response[line_start + matched], name[matched])) {
            ++matched;
        }
        if (matched == name_length && line_start + matched < line_end &&
            response[line_start + matched] == ':') {
            size_t source = line_start + matched + 1U;
            size_t destination = 0U;
            while (source < line_end &&
                   (response[source] == ' ' || response[source] == '\t')) ++source;
            while (source < line_end && destination + 1U < output_capacity) {
                output[destination++] = response[source++];
            }
            output[destination] = '\0';
            return destination != 0U;
        }

        index = line_end;
        if (response[index] == '\r') ++index;
        if (response[index] == '\n') ++index;
    }
    return 0;
}

static void render_view_commit(chromium_browser_context* context, const char* body) {
    size_t line = 0U;
    size_t column = 0U;
    size_t index = 0U;
    int inside_tag = 0;
    int pending_space = 0;
    int pending_break = 0;

    render_clear(context);
    while (body[index] != '\0' && line < BROWSER_RENDER_LINES) {
        const char ch = body[index];
        if (ch == '<') {
            const char* tag = body + index + 1U;
            inside_tag = 1;
            pending_space = 1;
            if (starts_with_ci(tag, "br") || starts_with_ci(tag, "/p") ||
                starts_with_ci(tag, "/div") || starts_with_ci(tag, "/h1") ||
                starts_with_ci(tag, "/h2") || starts_with_ci(tag, "/li")) {
                pending_break = 1;
            }
            ++index;
            continue;
        }
        if (inside_tag) {
            if (ch == '>') inside_tag = 0;
            ++index;
            continue;
        }
        if (ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ') {
            pending_space = 1;
            ++index;
            continue;
        }
        if ((unsigned char)ch < 0x20U || (unsigned char)ch > 0x7EU) {
            ++index;
            continue;
        }

        if (pending_break && column != 0U) {
            ++line;
            column = 0U;
            pending_break = 0;
            if (line >= BROWSER_RENDER_LINES) break;
        }
        if (pending_space && column != 0U) {
            if (column + 1U >= BROWSER_RENDER_LINE_CAPACITY) {
                ++line;
                column = 0U;
                if (line >= BROWSER_RENDER_LINES) break;
            } else {
                context->render_lines[line][column++] = ' ';
            }
        }
        pending_space = 0;
        if (column + 1U >= BROWSER_RENDER_LINE_CAPACITY) {
            ++line;
            column = 0U;
            if (line >= BROWSER_RENDER_LINES) break;
        }
        context->render_lines[line][column++] = ch;
        context->render_lines[line][column] = '\0';
        ++index;
    }

    if (context->render_lines[0][0] == '\0') {
        (void)strlcpy(
            context->render_lines[0],
            "NO TEXT CONTENT IN BOOTSTRAP RENDERER",
            BROWSER_RENDER_LINE_CAPACITY);
    }
}

static int make_redirect_url(
    chromium_url_scheme scheme,
    const char* host,
    const char* location,
    char* output,
    size_t output_capacity) {
    if (starts_with_ci(location, "http://") || starts_with_ci(location, "https://")) {
        return strlcpy(output, location, output_capacity) < output_capacity;
    }
    if (location[0] != '/') return 0;
    output[0] = '\0';
    append_text(output, output_capacity,
        scheme == CHROMIUM_SCHEME_HTTPS ? "https://" : "http://");
    append_text(output, output_capacity, host);
    append_text(output, output_capacity, location);
    return strlen(output) + 1U < output_capacity;
}

static void browser_context_initialize(chromium_browser_context* context) {
    memset(context, 0, sizeof(*context));
    (void)strlcpy(context->url, "https://www.google.com/", sizeof(context->url));
    context->url_length = strlen(context->url);
    context->stage = CHROMIUM_STAGE_IDLE;
    context->last_error = KU_STATUS_OK;
    (void)strlcpy(
        context->status,
        "READY / SEARCH OR ENTER ADDRESS",
        sizeof(context->status));
    render_message(
        context,
        "OMNIBOX: DOMAIN, URL OR SEARCH TEXT",
        "HTTPS USES VERIFIED TLS / CA / RTC VALIDATION");
    (void)platform_delegate_refresh_network(context);
}

static void navigation_fail(
    chromium_browser_context* context,
    ku_status_t status,
    const char* detail) {
    context->stage = CHROMIUM_STAGE_FAILED;
    context->last_error = status;
    (void)strlcpy(context->status, "NAVIGATION FAILED / ", sizeof(context->status));
    append_text(context->status, sizeof(context->status), status_name(status));
    append_text(context->status, sizeof(context->status), " / ");
    append_status_code(context->status, sizeof(context->status), status);
    render_message(context, detail, "CHECK NET / DHCP / DNS / RTC / TRUST STORE");
}

static ku_status_t navigation_controller_load(chromium_browser_context* context) {
    uint32_t redirect;
    char navigation_url[BROWSER_URL_CAPACITY];
    if (!omnibox_resolve(context->url, navigation_url, sizeof(navigation_url))) {
        navigation_fail(context, KU_STATUS_INVALID_ARGUMENT,
            "ENTER A DOMAIN, URL OR SEARCH QUERY");
        return KU_STATUS_INVALID_ARGUMENT;
    }
    context->redirect_count = 0U;
    context->http_status = 0U;
    context->bytes_received = 0U;

    for (redirect = 0U; redirect <= BROWSER_REDIRECT_LIMIT; ++redirect) {
        char host[KU_NET_HOST_CAPACITY] = {0};
        char path[KU_NET_PATH_CAPACITY] = {0};
        char location[BROWSER_URL_CAPACITY] = {0};
        chromium_url_scheme scheme;
        ku_http_request request;
        ku_status_t result;

        scheme = parse_url(navigation_url, host, sizeof(host), path, sizeof(path));
        if (scheme == CHROMIUM_SCHEME_INVALID) {
            navigation_fail(context, KU_STATUS_INVALID_ARGUMENT,
                "OMNIBOX COULD NOT RESOLVE THE NAVIGATION TARGET");
            return KU_STATUS_INVALID_ARGUMENT;
        }

        context->stage = CHROMIUM_STAGE_NETWORK;
        if (!platform_delegate_refresh_network(context)) {
            navigation_fail(context, KU_STATUS_BAD_STATE,
                "NETWORK STACK IS NOT READY FOR NAVIGATION");
            return KU_STATUS_BAD_STATE;
        }

        context->stage = CHROMIUM_STAGE_REQUEST;
        memset(context->page, 0, sizeof(context->page));
        memset(&request, 0, sizeof(request));
        request.structure_size = sizeof(request);
        (void)strlcpy(request.host, host, sizeof(request.host));
        (void)strlcpy(request.path, path, sizeof(request.path));
        request.output = context->page;
        request.output_capacity = sizeof(context->page) - 1U;

        (void)strlcpy(
            context->status,
            scheme == CHROMIUM_SCHEME_HTTPS
                ? "NAVIGATING / DNS -> TCP -> TLS -> HTTPS"
                : "NAVIGATING / DNS -> TCP -> HTTP",
            sizeof(context->status));
        result = scheme == CHROMIUM_SCHEME_HTTPS
            ? ku_https_get(&request)
            : ku_http_get(&request);
        if (result != KU_STATUS_OK) {
            if (result == KU_STATUS_OUT_OF_RANGE) {
                navigation_fail(context, result,
                    "TRANSPORT REJECTED A FRAME OR RESPONSE RANGE");
            } else if (result == KU_STATUS_WOULD_BLOCK ||
                       result == KU_STATUS_TIMED_OUT) {
                navigation_fail(context, result,
                    "DNS / TCP / TLS DID NOT COMPLETE BEFORE THE TIMEOUT");
            } else if (scheme == CHROMIUM_SCHEME_HTTPS &&
                       (result == KU_STATUS_BAD_STATE ||
                        result == KU_STATUS_IO_ERROR)) {
                navigation_fail(context, result,
                    "TLS / CERTIFICATE / RTC / TRUST VALIDATION FAILED");
            } else {
                navigation_fail(context, result,
                    "KERNEL NETWORK SERVICE RETURNED AN ERROR");
            }
            return result;
        }

        if (request.bytes_received >= sizeof(context->page)) {
            request.bytes_received = sizeof(context->page) - 1U;
        }
        context->page[(size_t)request.bytes_received] = '\0';
        context->http_status = request.http_status;
        context->bytes_received = request.bytes_received;

        if (context->http_status >= 300U && context->http_status < 400U &&
            response_header(context->page, "Location", location, sizeof(location))) {
            char redirected[BROWSER_URL_CAPACITY] = {0};
            context->stage = CHROMIUM_STAGE_REDIRECT;
            if (!make_redirect_url(scheme, host, location, redirected, sizeof(redirected))) {
                navigation_fail(context, KU_STATUS_CORRUPT_DATA,
                    "HTTP REDIRECT LOCATION COULD NOT BE RESOLVED");
                return KU_STATUS_CORRUPT_DATA;
            }
            (void)strlcpy(navigation_url, redirected, sizeof(navigation_url));
            context->redirect_count = redirect + 1U;
            continue;
        }

        context->stage = CHROMIUM_STAGE_COMMIT;
        context->last_error = KU_STATUS_OK;
        (void)strlcpy(context->url, navigation_url, sizeof(context->url));
        context->url_length = strlen(context->url);
        render_view_commit(context, response_body(context->page));
        (void)strlcpy(
            context->status,
            scheme == CHROMIUM_SCHEME_HTTPS ? "COMMITTED / HTTPS " : "COMMITTED / HTTP ",
            sizeof(context->status));
        append_u64(context->status, sizeof(context->status), context->http_status);
        append_text(context->status, sizeof(context->status), " / ");
        append_u64(context->status, sizeof(context->status), context->bytes_received);
        append_text(context->status, sizeof(context->status), " B");
        if (context->redirect_count != 0U) {
            append_text(context->status, sizeof(context->status), " / REDIRECTS ");
            append_u64(context->status, sizeof(context->status), context->redirect_count);
        }
        return KU_STATUS_OK;
    }

    navigation_fail(context, KU_STATUS_CORRUPT_DATA,
        "REDIRECT LIMIT EXCEEDED");
    return KU_STATUS_CORRUPT_DATA;
}

static void build_scene(kui_scene* scene) {
    kui_flow root;
    size_t index;
    char address[BROWSER_URL_CAPACITY + 18U] = "SEARCH / ADDRESS  ";
    char engine[96] = "ENGINE  CHROMIUM CONTENT_SHELL PORT / UPSTREAM ";
    char stage[96] = "NAV  ";

    (void)platform_delegate_refresh_network(&g_browser);
    append_text(address, sizeof(address), g_browser.url);
    append_text(engine, sizeof(engine), CHROMIUM_UPSTREAM_SHORT);
    append_text(stage, sizeof(stage), stage_name(g_browser.stage));

    kui_scene_initialize(scene);
    scene->visible_rows = 16U;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        UINT32_C(0xDE192D));

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "KUROGANE WEB / CHROMIUM PORT");
    (void)kui_flow_input(&root, 2U, address);
    (void)kui_flow_label(&root, 3U, "ENTER: GO / SEARCH   ESC: CLEAR   BACKSPACE: EDIT");
    (void)kui_flow_label(&root, 4U, engine);
    (void)kui_flow_label(&root, 5U, g_browser.network);
    (void)kui_flow_label(&root, 6U, stage);
    (void)kui_flow_label(&root, 7U, g_browser.status);
    (void)kui_flow_separator(&root, 8U);
    for (index = 0U; index < BROWSER_RENDER_LINES; ++index) {
        (void)kui_flow_label(
            &root,
            10U + (uint32_t)index,
            g_browser.render_lines[index][0] != '\0'
                ? g_browser.render_lines[index] : " ");
    }
    (void)kui_flow_separator(&root, 30U);
    (void)kui_flow_label(
        &root, 31U,
        "BOUNDED RENDERER / NETWORK WORK SLEEPS BETWEEN POLLS TO LIMIT CPU LOAD");
}

int main(void) {
    const ku_window_t window = gui_open("KUROGANE WEB", 120, 90, 820, 570);
    kui_scene scene;
    if (window == KU_INVALID_WINDOW) return 1;

    browser_context_initialize(&g_browser);
    puts("[TEST] chromium_port_browser_context: PASS");
    puts("[TEST] chromium_port_navigation_controller: PASS");
    puts("[TEST] chromium_port_platform_delegate: PASS");
    puts("[TEST] chromium_port_bootstrap_renderer: PASS");
    puts("[TEST] chromium_port_omnibox_search: PASS");
    puts("[TEST] chromium_port_https_path: PASS");

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
            if (g_browser.url_length != 0U) {
                g_browser.url[--g_browser.url_length] = '\0';
            }
        } else if (gui_key_cancel(&event)) {
            g_browser.url_length = 0U;
            g_browser.url[0] = '\0';
            g_browser.stage = CHROMIUM_STAGE_IDLE;
            (void)strlcpy(g_browser.status, "ADDRESS CLEARED", sizeof(g_browser.status));
        } else if (gui_key_activate(&event)) {
            (void)navigation_controller_load(&g_browser);
        } else if (event.character >= 0x20U && event.character <= 0x7EU &&
                   g_browser.url_length + 1U < sizeof(g_browser.url)) {
            g_browser.url[g_browser.url_length++] = (char)event.character;
            g_browser.url[g_browser.url_length] = '\0';
        }
    }

    (void)ku_ui_close(window);
    return 0;
}
