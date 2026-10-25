#include "../sdk/include/kurogane/flux_app_catalog.h"
#include <assert.h>
#include <string.h>

int main(void) {
    assert(strcmp(KU_FLUX_TITLE_HOME, "RED FLUX HOME") == 0);
    assert(strcmp(KU_FLUX_TITLE_TERMINAL, "RED FLUX TERMINAL") == 0);
    assert(strcmp(KU_FLUX_TITLE_FILES, "FILES") == 0);
    assert(strcmp(KU_FLUX_TITLE_PERFORMANCE, "FORGE CONTROL") == 0);
    assert(strcmp(KU_FLUX_TITLE_BROWSER, "KUROGANE WEB") == 0);
    assert(strcmp(KU_FLUX_TITLE_MONITOR, "SYSTEM MONITOR") == 0);
    assert(strcmp(KU_FLUX_TITLE_SETTINGS, "SETTINGS") == 0);
    assert(strcmp(KU_FLUX_TITLE_ABOUT, "ABOUT KUROGANEOS") == 0);
    assert(strcmp(KU_FLUX_EXEC_HOME, "/gui/launcher") == 0);
    assert(strcmp(KU_FLUX_EXEC_TERMINAL, "/gui/terminal") == 0);
    assert(strcmp(KU_FLUX_EXEC_NOTIFICATIONS, "/gui/notify") == 0);
    return 0;
}
