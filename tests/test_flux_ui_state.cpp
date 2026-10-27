#include "../kernel/ui/ui.hpp"
#include <kurogane/design.h>
#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
    assert(ui::desktop_theme_id() == KU_FLUX_THEME_RED);
    for (uint32_t id = 0U; id < (uint32_t)KU_FLUX_THEME_COUNT; ++id) {
        const auto preset = ku_flux_theme_palette(id);
        assert(ui::set_desktop_theme(id));
        assert(ui::desktop_theme_id() == id);
        const ui::Theme& actual = ui::default_theme();
        assert(actual.desktop == preset.desktop);
        assert(actual.panel == preset.panel);
        assert(actual.panel_alt == preset.background);
        assert(actual.border == preset.border);
        assert(actual.text == preset.text);
        assert(actual.text_muted == preset.text_muted);
        assert(actual.accent == preset.accent);
        assert(actual.danger == preset.danger);
    }
    assert(!ui::set_desktop_theme(KU_FLUX_THEME_COUNT));
    assert(ui::desktop_theme_id() == KU_FLUX_THEME_MONO);
    assert(ui::desktop_style_id() == KU_FLUX_STYLE_MODERN);
    assert(ui::set_desktop_style(KU_FLUX_STYLE_CLASSIC));
    assert(ui::desktop_style_id() == KU_FLUX_STYLE_CLASSIC);
    assert(ui::set_desktop_style(KU_FLUX_STYLE_MODERN));
    assert(ui::desktop_style_id() == KU_FLUX_STYLE_MODERN);
    assert(!ui::set_desktop_style(KU_FLUX_STYLE_COUNT));
    assert(ui::desktop_style_id() == KU_FLUX_STYLE_MODERN);
    std::puts("Flux 8 kernel theme and style state: PASS");
    return 0;
}
