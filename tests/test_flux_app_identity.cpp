#include "../kernel/ui/app_identity.hpp"
#include "../sdk/include/kurogane/flux_app_catalog.h"

#include <cassert>
#include <cstdio>

int main() {
    using windowing::app_identity::matches;

    assert(matches(KU_FLUX_TITLE_FILES, KU_FLUX_TITLE_FILES,
                   KU_FLUX_EXEC_FILES, KU_FLUX_EXEC_FILES));
    assert(matches(KU_FLUX_TITLE_TERMINAL, KU_FLUX_TITLE_TERMINAL,
                   KU_FLUX_EXEC_TERMINAL, KU_FLUX_EXEC_TERMINAL));
    // A same-session process may freely choose its normal window title.
    // Matching that title alone does not make it a trusted Dock app.
    assert(!matches(KU_FLUX_TITLE_FILES, KU_FLUX_TITLE_FILES,
                    KU_FLUX_EXEC_TERMINAL, KU_FLUX_EXEC_FILES));
    assert(!matches(KU_FLUX_TITLE_FILES, KU_FLUX_TITLE_FILES,
                    "/gui/files-backup", KU_FLUX_EXEC_FILES));
    assert(!matches(KU_FLUX_TITLE_FILES, KU_FLUX_TITLE_FILES,
                    "/gui/files/extra", KU_FLUX_EXEC_FILES));
    assert(!matches(KU_FLUX_TITLE_TERMINAL, KU_FLUX_TITLE_FILES,
                    KU_FLUX_EXEC_FILES, KU_FLUX_EXEC_FILES));
    assert(!matches(nullptr, KU_FLUX_TITLE_FILES,
                    KU_FLUX_EXEC_FILES, KU_FLUX_EXEC_FILES));
    assert(!matches(KU_FLUX_TITLE_FILES, KU_FLUX_TITLE_FILES,
                    nullptr, KU_FLUX_EXEC_FILES));

    std::puts("Flux Dock executable/title identity policy: PASS");
    return 0;
}
