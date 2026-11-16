#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../userspace/gui/launcher/search.h"

int main(void) {
    char query[FLUX_LAUNCHER_SEARCH_CAPACITY] = "";
    size_t index;

    assert(flux_search_matches("KUROGANE WEB", "native HTTP browser",
                               "/apps/flux-browser", ""));
    assert(flux_search_matches("KUROGANE WEB", "native HTTP browser",
                               "/apps/flux-browser", "web"));
    assert(flux_search_matches("TERMINAL", "shared shell / development",
                               "/apps/terminal", "DEV"));
    assert(flux_search_matches("FILES", "persistent root / applications",
                               "/apps/files", "files"));
    assert(!flux_search_matches("FILES", "persistent root / applications",
                                "/apps/files", "camera"));
    assert(flux_search_contains("SETTINGS", "ting"));
    assert(!flux_search_contains("SETTINGS", "tings extra"));
    assert(!flux_search_contains(NULL, "abc"));

    assert(flux_search_edit(query, sizeof(query), 'F', 0, 0, 0));
    assert(flux_search_edit(query, sizeof(query), 'i', 0, 0, 0));
    assert(strcmp(query, "Fi") == 0);
    assert(!flux_search_edit(query, sizeof(query), 'x', 0, 0, 1));
    assert(!flux_search_edit(query, sizeof(query), '\n', 0, 0, 0));
    assert(!flux_search_edit(query, sizeof(query), 0xC5U, 0, 0, 0));
    assert(flux_search_edit(query, sizeof(query), 0U, 1, 0, 0));
    assert(strcmp(query, "F") == 0);
    assert(flux_search_edit(query, sizeof(query), 0U, 0, 1, 0));
    assert(query[0] == '\0');
    assert(!flux_search_edit(query, sizeof(query), 0U, 0, 1, 0));
    assert(!flux_search_edit(query, sizeof(query), 0U, 1, 0, 0));

    for (index = 0U; index < sizeof(query) - 1U; ++index)
        assert(flux_search_edit(query, sizeof(query), 'x', 0, 0, 0));
    assert(query[sizeof(query) - 1U] == '\0');
    assert(!flux_search_edit(query, sizeof(query), 'y', 0, 0, 0));
    assert(flux_search_edit(query, sizeof(query), 0U, 1, 0, 0));
    assert(strlen(query) == sizeof(query) - 2U);

    puts("Flux Home instant app search: PASS");
    return 0;
}
