#include "../common.h"

#include <kurogane/network.h>

#define BROWSER_ADDRESS_CAPACITY 224U
#define BROWSER_HISTORY_CAPACITY 8U
#define BROWSER_TEXT_LINES 8U
#define BROWSER_LINKS 6U
#define BROWSER_LINK_LABEL_CAPACITY 58U
#define BROWSER_LINK_URL_CAPACITY 224U
#define BROWSER_REDIRECT_LIMIT 3U

#define VIEW_SHELL 1U
#define VIEW_BACK 2U
#define VIEW_HOME 3U
#define VIEW_RELOAD 4U
#define VIEW_ADDRESS 5U
#define VIEW_TOP_SEPARATOR 6U
#define VIEW_TITLE 7U
#define VIEW_META 8U
#define VIEW_TEXT_BASE 10U
#define VIEW_LINK_SEPARATOR 18U
#define VIEW_LINK_BASE 20U
#define VIEW_STATUS 31U

typedef struct browser_url {
    int secure;
    char host[KU_NET_HOST_CAPACITY];
    char path[KU_NET_PATH_CAPACITY];
    char canonical[BROWSER_ADDRESS_CAPACITY];
} browser_url;

typedef struct browser_link {
    char label[BROWSER_LINK_LABEL_CAPACITY];
    char url[BROWSER_LINK_URL_CAPACITY];
} browser_link;

static uint8_t g_response[KU_HTTP_RESPONSE_CAPACITY_LIMIT + 1U];
static char g_address[BROWSER_ADDRESS_CAPACITY] = "https://docs.kuroganeos.dev/";
static char g_loaded_url[BROWSER_ADDRESS_CAPACITY] = "";
static char g_history[BROWSER_HISTORY_CAPACITY][BROWSER_ADDRESS_CAPACITY];
static size_t g_history_count = 0U;
static char g_title[KU_UI_WIDGET_TEXT_CAPACITY] = "Kurogane Web";
static char g_meta[KU_UI_WIDGET_TEXT_CAPACITY] = "Native HTTP / HTTPS surface";
static char g_status[KU_UI_WIDGET_TEXT_CAPACITY] = "WEB SHELL READY";
static char g_text[BROWSER_TEXT_LINES][KU_UI_WIDGET_TEXT_CAPACITY];
static browser_link g_links[BROWSER_LINKS];
static size_t g_link_count = 0U;
static uint32_t g_selected = VIEW_ADDRESS;
static int g_editing = 0;

static int ascii_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static char ascii_lower(char value) {
    return value >= 'A' && value <= 'Z'
        ? (char)(value + ('a' - 'A')) : value;
}

static int starts_with(const char* text, const char* prefix) {
    if (text == NULL || prefix == NULL) return 0;
    while (*prefix != '\0') {
        if (*text == '\0' || *text != *prefix) return 0;
        ++text;
        ++prefix;
    }
    return 1;
}

static int starts_with_case(const char* text, const char* prefix) {
    if (text == NULL || prefix == NULL) return 0;
    while (*prefix != '\0') {
        if (*text == '\0' || ascii_lower(*text) != ascii_lower(*prefix)) return 0;
        ++text;
        ++prefix;
    }
    return 1;
}

static const char* find_case(const char* text, const char* needle) {
    if (text == NULL || needle == NULL) return NULL;
    if (*needle == '\0') return text;
    while (*text != '\0') {
        if (starts_with_case(text, needle)) return text;
        ++text;
    }
    return NULL;
}

static int append_part(char* output, size_t capacity, const char* text) {
    size_t used;
    size_t incoming;
    if (output == NULL || text == NULL || capacity == 0U) return 0;
    used = strlen(output);
    incoming = strlen(text);
    if (used + incoming + 1U > capacity) return 0;
    memcpy(output + used, text, incoming + 1U);
    return 1;
}

static void set_status(const char* prefix, uint64_t value, const char* suffix) {
    char number[24];
    g_status[0] = '\0';
    gui_append_text(g_status, sizeof(g_status), prefix);
    gui_u64(number, sizeof(number), value);
    gui_append_text(g_status, sizeof(g_status), number);
    gui_append_text(g_status, sizeof(g_status), suffix);
}

static void clear_document(void) {
    size_t index;
    (void)strlcpy(g_title, "Kurogane Web", sizeof(g_title));
    (void)strlcpy(g_meta, "Native HTTP / HTTPS surface", sizeof(g_meta));
    for (index = 0U; index < BROWSER_TEXT_LINES; ++index) g_text[index][0] = '\0';
    for (index = 0U; index < BROWSER_LINKS; ++index) {
        g_links[index].label[0] = '\0';
        g_links[index].url[0] = '\0';
    }
    g_link_count = 0U;
}

static int network_is_ready(void) {
    ku_network_status status;
    memset(&status, 0, sizeof(status));
    status.structure_size = sizeof(status);
    if (ku_network_get_status(&status) != KU_STATUS_OK) return 0;
    return status.ready != 0U;
}

static int parse_url(const char* input, browser_url* output) {
    char source[BROWSER_ADDRESS_CAPACITY];
    const char* cursor;
    const char* slash;
    size_t host_length;
    size_t length;
    size_t index;

    if (input == NULL || output == NULL) return 0;
    while (ascii_space(*input)) ++input;
    (void)strlcpy(source, input, sizeof(source));
    length = strlen(source);
    while (length != 0U && ascii_space(source[length - 1U])) {
        source[--length] = '\0';
    }
    if (source[0] == '\0') return 0;

    memset(output, 0, sizeof(*output));
    if (starts_with_case(source, "https://")) {
        output->secure = 1;
        cursor = source + 8U;
    } else if (starts_with_case(source, "http://")) {
        output->secure = 0;
        cursor = source + 7U;
    } else {
        output->secure = 1;
        cursor = source;
    }

    slash = strchr(cursor, '/');
    host_length = slash != NULL ? (size_t)(slash - cursor) : strlen(cursor);
    if (host_length == 0U || host_length >= sizeof(output->host)) return 0;
    for (index = 0U; index < host_length; ++index) {
        if (cursor[index] == ':' || ascii_space(cursor[index])) return 0;
        output->host[index] = ascii_lower(cursor[index]);
    }
    output->host[host_length] = '\0';

    if (slash == NULL) {
        (void)strlcpy(output->path, "/", sizeof(output->path));
    } else {
        length = strlen(slash);
        if (length == 0U || length >= sizeof(output->path)) return 0;
        memcpy(output->path, slash, length + 1U);
    }

    output->canonical[0] = '\0';
    if (!append_part(output->canonical, sizeof(output->canonical),
                     output->secure ? "https://" : "http://") ||
        !append_part(output->canonical, sizeof(output->canonical), output->host) ||
        !append_part(output->canonical, sizeof(output->canonical), output->path)) {
        return 0;
    }
    return 1;
}

static int response_body(
    uint8_t* response,
    size_t size,
    char** body,
    size_t* body_size) {
    size_t index;
    if (response == NULL || body == NULL || body_size == NULL) return 0;
    for (index = 0U; index + 3U < size; ++index) {
        if (response[index] == '\r' && response[index + 1U] == '\n' &&
            response[index + 2U] == '\r' && response[index + 3U] == '\n') {
            *body = (char*)response + index + 4U;
            *body_size = size - index - 4U;
            return 1;
        }
    }
    return 0;
}

static int response_header(
    const char* response,
    const char* name,
    char* output,
    size_t capacity) {
    const size_t name_length = strlen(name);
    const char* line = response;
    if (response == NULL || output == NULL || capacity == 0U) return 0;
    output[0] = '\0';

    while (*line != '\0') {
        const char* end = strstr(line, "\r\n");
        const size_t line_length = end != NULL
            ? (size_t)(end - line) : strlen(line);
        if (line_length == 0U) break;
        if (line_length > name_length && starts_with_case(line, name) &&
            line[name_length] == ':') {
            const char* value = line + name_length + 1U;
            size_t value_length;
            while (*value == ' ' || *value == '\t') ++value;
            value_length = line_length - (size_t)(value - line);
            if (value_length >= capacity) value_length = capacity - 1U;
            memcpy(output, value, value_length);
            output[value_length] = '\0';
            return value_length != 0U;
        }
        if (end == NULL) break;
        line = end + 2U;
    }
    return 0;
}

static size_t decode_entity(const char* source, char* output) {
    if (starts_with(source, "&amp;")) { *output = '&'; return 5U; }
    if (starts_with(source, "&lt;")) { *output = '<'; return 4U; }
    if (starts_with(source, "&gt;")) { *output = '>'; return 4U; }
    if (starts_with(source, "&quot;")) { *output = '"'; return 6U; }
    if (starts_with(source, "&#39;")) { *output = '\''; return 5U; }
    if (starts_with(source, "&nbsp;")) { *output = ' '; return 6U; }
    *output = *source;
    return 1U;
}

static void normalize_text(
    const char* begin,
    const char* end,
    char* output,
    size_t capacity) {
    size_t written = 0U;
    int in_tag = 0;
    int pending_space = 0;
    const char* cursor = begin;
    if (begin == NULL || end == NULL || output == NULL || capacity == 0U) return;

    while (cursor < end && *cursor != '\0' && written + 1U < capacity) {
        char value;
        size_t consumed = 1U;
        if (*cursor == '<') { in_tag = 1; ++cursor; continue; }
        if (*cursor == '>' && in_tag) {
            in_tag = 0;
            pending_space = written != 0U;
            ++cursor;
            continue;
        }
        if (in_tag) { ++cursor; continue; }
        if (*cursor == '&') consumed = decode_entity(cursor, &value);
        else value = *cursor;
        cursor += consumed;
        if (ascii_space(value)) {
            pending_space = written != 0U;
            continue;
        }
        if (pending_space && written + 1U < capacity) output[written++] = ' ';
        pending_space = 0;
        output[written++] = value;
    }
    while (written != 0U && output[written - 1U] == ' ') --written;
    output[written] = '\0';
}

static int resolve_href(
    const browser_url* base,
    const char* href,
    char* output,
    size_t capacity) {
    char candidate[BROWSER_LINK_URL_CAPACITY];
    browser_url parsed;
    if (base == NULL || href == NULL || output == NULL || capacity == 0U) return 0;
    while (ascii_space(*href)) ++href;
    if (*href == '\0' || *href == '#') return 0;

    candidate[0] = '\0';
    if (starts_with_case(href, "https://") || starts_with_case(href, "http://")) {
        (void)strlcpy(candidate, href, sizeof(candidate));
    } else if (starts_with(href, "//")) {
        (void)strlcpy(candidate, base->secure ? "https:" : "http:", sizeof(candidate));
        if (!append_part(candidate, sizeof(candidate), href)) return 0;
    } else {
        char directory[KU_NET_PATH_CAPACITY];
        char* last;
        if (!append_part(candidate, sizeof(candidate), base->secure ? "https://" : "http://") ||
            !append_part(candidate, sizeof(candidate), base->host)) return 0;
        if (*href == '/') {
            if (!append_part(candidate, sizeof(candidate), href)) return 0;
        } else {
            (void)strlcpy(directory, base->path, sizeof(directory));
            last = strrchr(directory, '/');
            if (last == NULL) (void)strlcpy(directory, "/", sizeof(directory));
            else last[1] = '\0';
            if (!append_part(candidate, sizeof(candidate), directory) ||
                !append_part(candidate, sizeof(candidate), href)) return 0;
        }
    }

    if (!parse_url(candidate, &parsed)) return 0;
    (void)strlcpy(output, parsed.canonical, capacity);
    return 1;
}

static void parse_title(const char* html) {
    const char* open = find_case(html, "<title");
    const char* begin;
    const char* end;
    if (open == NULL) return;
    begin = strchr(open, '>');
    if (begin == NULL) return;
    ++begin;
    end = find_case(begin, "</title>");
    if (end == NULL) return;
    normalize_text(begin, end, g_title, sizeof(g_title));
    if (g_title[0] == '\0') (void)strlcpy(g_title, "Kurogane Web", sizeof(g_title));
}

static void parse_document_text(const char* html) {
    char plain[KU_UI_WIDGET_TEXT_CAPACITY * BROWSER_TEXT_LINES];
    const char* cursor = html;
    size_t written = 0U;
    size_t line = 0U;
    int in_tag = 0;
    int pending_space = 0;

    memset(plain, 0, sizeof(plain));
    while (*cursor != '\0' && written + 1U < sizeof(plain)) {
        char value;
        size_t consumed = 1U;
        if (*cursor == '<') {
            if (starts_with_case(cursor, "<script")) {
                const char* close = find_case(cursor, "</script>");
                cursor = close != NULL ? close + 9U : cursor + strlen(cursor);
                pending_space = 1;
                continue;
            }
            if (starts_with_case(cursor, "<style")) {
                const char* close = find_case(cursor, "</style>");
                cursor = close != NULL ? close + 8U : cursor + strlen(cursor);
                pending_space = 1;
                continue;
            }
            in_tag = 1;
            pending_space = written != 0U;
            ++cursor;
            continue;
        }
        if (*cursor == '>' && in_tag) { in_tag = 0; ++cursor; continue; }
        if (in_tag) { ++cursor; continue; }
        if (*cursor == '&') consumed = decode_entity(cursor, &value);
        else value = *cursor;
        cursor += consumed;
        if (ascii_space(value)) {
            pending_space = written != 0U;
            continue;
        }
        if (pending_space && written + 1U < sizeof(plain)) plain[written++] = ' ';
        pending_space = 0;
        plain[written++] = value;
    }
    plain[written] = '\0';

    for (line = 0U; line < BROWSER_TEXT_LINES; ++line) g_text[line][0] = '\0';
    line = 0U;
    cursor = plain;
    while (*cursor != '\0' && line < BROWSER_TEXT_LINES) {
        const size_t remaining = strlen(cursor);
        size_t take = remaining;
        size_t split;
        if (take >= KU_UI_WIDGET_TEXT_CAPACITY) {
            take = KU_UI_WIDGET_TEXT_CAPACITY - 1U;
            split = take;
            while (split != 0U && cursor[split] != ' ') --split;
            if (split != 0U) take = split;
        }
        memcpy(g_text[line], cursor, take);
        g_text[line][take] = '\0';
        cursor += take;
        while (*cursor == ' ') ++cursor;
        ++line;
    }
}

static void parse_links(const char* html, const browser_url* base) {
    const char* cursor = html;
    g_link_count = 0U;

    while (g_link_count < BROWSER_LINKS) {
        const char* anchor = find_case(cursor, "<a");
        const char* tag_end;
        const char* href;
        const char* value;
        const char* close;
        char quote = '\0';
        char raw[BROWSER_LINK_URL_CAPACITY];
        size_t raw_length;

        if (anchor == NULL) break;
        tag_end = strchr(anchor, '>');
        if (tag_end == NULL) break;
        href = find_case(anchor, "href");
        if (href == NULL || href >= tag_end) {
            cursor = tag_end + 1U;
            continue;
        }
        href += 4U;
        while (href < tag_end && ascii_space(*href)) ++href;
        if (href >= tag_end || *href != '=') {
            cursor = tag_end + 1U;
            continue;
        }
        ++href;
        while (href < tag_end && ascii_space(*href)) ++href;
        if (href >= tag_end) break;
        if (*href == '\'' || *href == '"') quote = *href++;
        value = href;
        while (href < tag_end &&
               ((quote != '\0' && *href != quote) ||
                (quote == '\0' && !ascii_space(*href) && *href != '>'))) {
            ++href;
        }
        raw_length = (size_t)(href - value);
        if (raw_length == 0U || raw_length >= sizeof(raw)) {
            cursor = tag_end + 1U;
            continue;
        }
        memcpy(raw, value, raw_length);
        raw[raw_length] = '\0';
        if (!resolve_href(base, raw, g_links[g_link_count].url,
                          sizeof(g_links[g_link_count].url))) {
            cursor = tag_end + 1U;
            continue;
        }

        close = find_case(tag_end + 1U, "</a>");
        if (close == NULL) close = tag_end + 1U;
        normalize_text(tag_end + 1U, close,
                       g_links[g_link_count].label,
                       sizeof(g_links[g_link_count].label));
        if (g_links[g_link_count].label[0] == '\0') {
            (void)strlcpy(g_links[g_link_count].label,
                          g_links[g_link_count].url,
                          sizeof(g_links[g_link_count].label));
        }
        ++g_link_count;
        cursor = close;
        if (starts_with_case(cursor, "</a>")) cursor += 4U;
    }
}

static void history_push(const char* url) {
    size_t index;
    if (url == NULL || url[0] == '\0') return;
    if (g_history_count != 0U &&
        strcmp(g_history[g_history_count - 1U], url) == 0) return;
    if (g_history_count == BROWSER_HISTORY_CAPACITY) {
        for (index = 1U; index < BROWSER_HISTORY_CAPACITY; ++index) {
            (void)strlcpy(g_history[index - 1U], g_history[index],
                          sizeof(g_history[index - 1U]));
        }
        --g_history_count;
    }
    (void)strlcpy(g_history[g_history_count], url,
                  sizeof(g_history[g_history_count]));
    ++g_history_count;
}

static int fetch_page(
    const browser_url* target,
    browser_url* final_url,
    unsigned redirects) {
    ku_http_request request;
    ku_status_t status;
    char location[BROWSER_LINK_URL_CAPACITY];
    char redirect_url[BROWSER_LINK_URL_CAPACITY];
    char* body = NULL;
    size_t body_size = 0U;
    browser_url redirected;

    if (target == NULL || final_url == NULL) return 0;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    (void)strlcpy(request.host, target->host, sizeof(request.host));
    (void)strlcpy(request.path, target->path, sizeof(request.path));
    request.output = g_response;
    request.output_capacity = KU_HTTP_RESPONSE_CAPACITY_LIMIT;

    status = target->secure ? ku_https_get(&request) : ku_http_get(&request);
    if (status != KU_STATUS_OK) {
        set_status("NETWORK ERROR ", (uint64_t)(uint32_t)(-status), "");
        return 0;
    }
    if (request.bytes_received >= sizeof(g_response)) {
        (void)strlcpy(g_status, "PAGE EXCEEDS WEB BUFFER", sizeof(g_status));
        return 0;
    }
    g_response[(size_t)request.bytes_received] = '\0';

    if ((request.http_status == 301U || request.http_status == 302U ||
         request.http_status == 303U || request.http_status == 307U ||
         request.http_status == 308U) &&
        redirects < BROWSER_REDIRECT_LIMIT &&
        response_header((const char*)g_response, "Location",
                        location, sizeof(location)) &&
        resolve_href(target, location, redirect_url, sizeof(redirect_url)) &&
        parse_url(redirect_url, &redirected)) {
        return fetch_page(&redirected, final_url, redirects + 1U);
    }

    if (request.http_status < 200U || request.http_status >= 300U) {
        set_status("HTTP ", request.http_status, "");
        return 0;
    }
    if (!response_body(g_response, (size_t)request.bytes_received,
                       &body, &body_size)) {
        (void)strlcpy(g_status, "INVALID HTTP RESPONSE", sizeof(g_status));
        return 0;
    }

    body[body_size] = '\0';
    *final_url = *target;
    clear_document();
    parse_title(body);
    parse_document_text(body);
    parse_links(body, final_url);
    set_status("LOADED ", body_size, " BYTES");
    return 1;
}

static int navigate_to(const char* requested, int remember_current) {
    browser_url target;
    browser_url final_url;
    if (!parse_url(requested, &target)) {
        (void)strlcpy(g_status, "INVALID URL", sizeof(g_status));
        return 0;
    }
    if (!network_is_ready()) {
        (void)strlcpy(g_status,
                      "NETWORK OFFLINE / PRESS R TO RETRY",
                      sizeof(g_status));
        return 0;
    }

    (void)strlcpy(g_status, "CONNECTING...", sizeof(g_status));
    if (!fetch_page(&target, &final_url, 0U)) return 0;

    if (remember_current && g_loaded_url[0] != '\0' &&
        strcmp(g_loaded_url, final_url.canonical) != 0) {
        history_push(g_loaded_url);
    }
    (void)strlcpy(g_loaded_url, final_url.canonical, sizeof(g_loaded_url));
    (void)strlcpy(g_address, final_url.canonical, sizeof(g_address));
    g_editing = 0;
    g_selected = g_link_count != 0U ? VIEW_LINK_BASE : VIEW_ADDRESS;
    g_meta[0] = '\0';
    gui_append_text(g_meta, sizeof(g_meta),
                    final_url.secure ? "TLS 1.2  |  " : "HTTP  |  ");
    gui_append_text(g_meta, sizeof(g_meta), final_url.host);
    return 1;
}

static void navigate_back(void) {
    char target[BROWSER_ADDRESS_CAPACITY];
    if (g_history_count == 0U) {
        (void)strlcpy(g_status, "NO BACK HISTORY", sizeof(g_status));
        return;
    }
    (void)strlcpy(target, g_history[g_history_count - 1U], sizeof(target));
    --g_history_count;
    (void)navigate_to(target, 0);
}

static void select_next(int direction) {
    uint32_t choices[4U + BROWSER_LINKS];
    size_t count = 0U;
    size_t index;
    choices[count++] = VIEW_BACK;
    choices[count++] = VIEW_HOME;
    choices[count++] = VIEW_RELOAD;
    choices[count++] = VIEW_ADDRESS;
    for (index = 0U; index < g_link_count; ++index) {
        choices[count++] = VIEW_LINK_BASE + (uint32_t)index;
    }
    for (index = 0U; index < count && choices[index] != g_selected; ++index) {}
    if (index == count) index = 0U;
    else if (direction > 0) index = (index + 1U) % count;
    else index = index == 0U ? count - 1U : index - 1U;
    g_selected = choices[index];
}

static void activate(uint32_t id) {
    if (id == VIEW_BACK) navigate_back();
    else if (id == VIEW_HOME) (void)navigate_to("https://docs.kuroganeos.dev/", 1);
    else if (id == VIEW_RELOAD) {
        (void)navigate_to(g_loaded_url[0] != '\0' ? g_loaded_url : g_address, 0);
    } else if (id == VIEW_ADDRESS) {
        g_editing = 1;
    } else if (id >= VIEW_LINK_BASE && id < VIEW_LINK_BASE + BROWSER_LINKS) {
        const size_t link = (size_t)(id - VIEW_LINK_BASE);
        if (link < g_link_count) (void)navigate_to(g_links[link].url, 1);
    }
}

static void build_scene(kui_scene* scene) {
    size_t index;
    char address_label[KU_UI_WIDGET_TEXT_CAPACITY];
    kui_scene_initialize(scene);
    scene->visible_rows = 12U;
    gui_apply_forged_theme(scene, 1);
    (void)kui_scene_set_cursor(
        scene, g_editing ? KU_UI_CURSOR_TEXT : KU_UI_CURSOR_HAND);

    (void)kui_scene_add(scene, VIEW_SHELL, 0U, KUI_VIEW_PANEL,
                        "KUROGANE WEB / SECURE NAVIGATION");
    (void)kui_scene_set_icon(scene, VIEW_SHELL, KU_ICON_APPLICATION_BROWSER);
    (void)kui_scene_add(scene, VIEW_BACK, 0U, KUI_VIEW_BUTTON, "Back");
    (void)kui_scene_set_icon(scene, VIEW_BACK, KU_ICON_NAVIGATION_BACK);
    (void)kui_scene_add(scene, VIEW_HOME, 0U, KUI_VIEW_BUTTON, "Home");
    (void)kui_scene_set_icon(scene, VIEW_HOME, KU_ICON_NAVIGATION_HOME);
    (void)kui_scene_add(scene, VIEW_RELOAD, 0U, KUI_VIEW_BUTTON, "Reload");
    (void)kui_scene_set_icon(scene, VIEW_RELOAD, KU_ICON_ACTION_REFRESH);

    address_label[0] = '\0';
    gui_append_text(address_label, sizeof(address_label),
                    g_editing ? "EDIT  " : "URL   ");
    gui_append_text(address_label, sizeof(address_label), g_address);
    (void)kui_scene_add(scene, VIEW_ADDRESS, 0U, KUI_VIEW_INPUT, address_label);
    (void)kui_scene_set_icon(scene, VIEW_ADDRESS, KU_ICON_ACTION_SEARCH);
    (void)kui_scene_add(scene, VIEW_TOP_SEPARATOR, 0U, KUI_VIEW_SEPARATOR, "");
    (void)kui_scene_add(scene, VIEW_TITLE, 0U, KUI_VIEW_LABEL, g_title);
    (void)kui_scene_set_icon(scene, VIEW_TITLE, KU_ICON_FILE_TYPE_HTML);
    (void)kui_scene_add(scene, VIEW_META, 0U, KUI_VIEW_LABEL, g_meta);

    for (index = 0U; index < BROWSER_TEXT_LINES; ++index) {
        if (g_text[index][0] != '\0') {
            (void)kui_scene_add(scene, VIEW_TEXT_BASE + (uint32_t)index,
                                0U, KUI_VIEW_LABEL, g_text[index]);
        }
    }
    if (g_link_count != 0U) {
        (void)kui_scene_add(scene, VIEW_LINK_SEPARATOR,
                            0U, KUI_VIEW_SEPARATOR, "");
        for (index = 0U; index < g_link_count; ++index) {
            (void)kui_scene_add(scene, VIEW_LINK_BASE + (uint32_t)index,
                                0U, KUI_VIEW_BUTTON, g_links[index].label);
            (void)kui_scene_set_icon(scene,
                                     VIEW_LINK_BASE + (uint32_t)index,
                                     KU_ICON_ACTION_OPEN);
        }
    }
    (void)kui_scene_add(scene, VIEW_STATUS, 0U, KUI_VIEW_LABEL, g_status);
    (void)kui_scene_set_icon(scene, VIEW_STATUS,
                             network_is_ready()
                                 ? KU_ICON_STATUS_ONLINE
                                 : KU_ICON_STATUS_WARNING);
    (void)kui_scene_select(scene, g_selected);
}

static int present(ku_window_t window, kui_scene* scene) {
    build_scene(scene);
    return kui_scene_present(window, scene) == KU_STATUS_OK;
}

static void edit_character(uint32_t character) {
    size_t length;
    if (character < 32U || character > 126U) return;
    length = strlen(g_address);
    if (length + 1U >= sizeof(g_address)) return;
    g_address[length] = (char)character;
    g_address[length + 1U] = '\0';
}

int main(void) {
    ku_window_t window;
    kui_scene scene;
    ku_ui_event event;

    clear_document();
    window = gui_open("KUROGANE WEB", 330, 94, 870, 650);
    if (window == KU_INVALID_WINDOW) return 1;

    // Important 5.0 launch contract: the browser surface becomes visible
    // before any synchronous DNS/TCP/TLS work begins. A slow or unavailable
    // network therefore cannot masquerade as a failed application launch.
    (void)strlcpy(g_status, "WEB SHELL ONLINE / CHECKING NETWORK",
                  sizeof(g_status));
    if (!present(window, &scene)) {
        (void)ku_ui_close(window);
        return 2;
    }
    puts("[TEST] kurogane5_web_surface: PASS");
    puts("[TEST] desktop_browser_ring3: PASS");
    (void)kuro_sleep(2U);

    if (network_is_ready()) {
        (void)strlcpy(g_status, "OPENING SECURE HOME...", sizeof(g_status));
        (void)present(window, &scene);
        (void)kuro_sleep(1U);
        if (navigate_to(g_address, 0)) {
            puts("[TEST] kurogane5_web_initial_navigation: PASS");
        }
    } else {
        (void)strlcpy(g_status,
                      "NETWORK OFFLINE / WEB REMAINS INTERACTIVE",
                      sizeof(g_status));
        puts("[TEST] kurogane5_web_offline_shell: PASS");
    }

    for (;;) {
        uint32_t hit;
        if (!present(window, &scene)) break;
        if (gui_wait_event(window, &event) < 0) break;
        if (event.type == KU_UI_EVENT_CLOSE) break;

        if (event.type == KU_UI_EVENT_POINTER) {
            hit = gui_scene_hit_test_local(&scene, &event);
            if (hit != 0U) {
                g_selected = hit;
                activate(hit);
            }
            continue;
        }
        if (event.type != KU_UI_EVENT_KEY) continue;

        if (g_editing) {
            if (gui_key_activate(&event)) {
                (void)navigate_to(g_address, 1);
            } else if (gui_key_cancel(&event)) {
                if (g_loaded_url[0] != '\0') {
                    (void)strlcpy(g_address, g_loaded_url, sizeof(g_address));
                }
                g_editing = 0;
            } else if (event.key == KU_UI_KEY_BACKSPACE) {
                const size_t length = strlen(g_address);
                if (length != 0U) g_address[length - 1U] = '\0';
            } else {
                edit_character(event.character);
            }
            continue;
        }

        if (gui_key_cancel(&event)) break;
        if (gui_key_tab(&event) || gui_key_down(&event) || gui_key_right(&event)) {
            select_next(1);
        } else if (gui_key_up(&event) || gui_key_left(&event)) {
            select_next(-1);
        } else if (gui_key_activate(&event)) {
            activate(g_selected);
        } else if (event.character == 'e' || event.character == 'E') {
            g_selected = VIEW_ADDRESS;
            g_editing = 1;
        } else if (event.character == 'r' || event.character == 'R') {
            activate(VIEW_RELOAD);
        } else if (event.character == 'h' || event.character == 'H') {
            activate(VIEW_HOME);
        } else if (event.character == 'b' || event.character == 'B') {
            activate(VIEW_BACK);
        }
    }

    (void)ku_ui_close(window);
    return 0;
}
