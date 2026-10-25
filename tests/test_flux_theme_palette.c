#include <kurogane/design.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Shared exact source is compiled as hosted C11 and C++17. */
int main(void) {
    uint32_t id;
    uint32_t accents[KU_FLUX_THEME_COUNT];
    for (id = 0U; id < KU_FLUX_THEME_COUNT; ++id) {
        const ku_flux_palette palette = ku_flux_theme_palette(id);
        assert(palette.accent != 0U);
        assert(palette.accent_focus != 0U);
        assert(palette.text != palette.background);
        assert(palette.desktop != palette.panel);
        assert(palette.border != palette.background);
        assert(palette.accent != palette.accent_deep);
        assert((palette.accent & 0xFF000000U) == 0U);
        assert(strlen(ku_flux_theme_name(id)) > 8U);
        for (uint32_t other = 0U; other < id; ++other) {
            assert(accents[other] != palette.accent);
        }
        accents[id] = palette.accent;
    }
    const ku_flux_palette red_input = ku_flux_theme_palette(KU_FLUX_THEME_RED);
    const ku_flux_palette violet_input = ku_flux_theme_palette(KU_FLUX_THEME_VIOLET);
    const ku_flux_scene_colors remapped = ku_flux_resolve_scene_colors(
        red_input.background, red_input.text, red_input.accent,
        KU_FLUX_THEME_VIOLET);
    assert(remapped.background == violet_input.background);
    assert(remapped.foreground == violet_input.text);
    assert(remapped.accent == violet_input.accent);
    /* Custom palettes must never be silently rewritten. */
    const ku_flux_scene_colors custom = ku_flux_resolve_scene_colors(
        0x123456U, 0xEFEFEFU, 0xAABBCCU, KU_FLUX_THEME_VIOLET);
    assert(custom.background == 0x123456U);
    assert(custom.foreground == 0xEFEFEFU);
    assert(custom.accent == 0xAABBCCU);
    const ku_flux_palette fallback = ku_flux_theme_palette(UINT32_MAX);
    const ku_flux_palette red = ku_flux_theme_palette(KU_FLUX_THEME_RED);
    assert(fallback.accent == red.accent);
    assert(strcmp(ku_flux_theme_name(UINT32_MAX),
                  ku_flux_theme_name(KU_FLUX_THEME_RED)) == 0);
    return 0;
}
