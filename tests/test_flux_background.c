#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kurogane/design.h>
#include <kurogane/desktop.h>

int main(void) {
    assert(KU_FLUX_BACKGROUND_COUNT == 3);
    assert(KU_FLUX_BACKGROUND_FORGED == 0);
    assert(KU_FLUX_BACKGROUND_GRID == 1);
    assert(KU_FLUX_BACKGROUND_MINIMAL == 2);
    assert(strcmp(ku_flux_background_name(KU_FLUX_BACKGROUND_FORGED), "FORGED STEEL") == 0);
    assert(strcmp(ku_flux_background_name(KU_FLUX_BACKGROUND_GRID), "GRAPHITE GRID") == 0);
    assert(strcmp(ku_flux_background_name(KU_FLUX_BACKGROUND_MINIMAL), "MINIMAL DARK") == 0);
    assert(strcmp(ku_flux_background_name(KU_FLUX_BACKGROUND_COUNT), "FORGED STEEL") == 0);
    assert(KU_DESKTOP_BACKGROUND_QUERY == 0);
    assert(KU_DESKTOP_BACKGROUND_SET == 1);
    assert(KU_SYS_DESKTOP_BACKGROUND == 73);
    assert(sizeof(ku_desktop_background_request) == 16U);
    puts("Flux 8 native background IDs and ABI: PASS");
    return 0;
}
