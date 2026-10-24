#ifndef KUROGANE_DESIGN_H
#define KUROGANE_DESIGN_H

#include <stdint.h>

/*
 * Flux UI 2.0 design tokens.
 *
 * These values are shared by the kernel compositor and Ring-3 libui so the
 * shell, window chrome and applications do not grow separate visual dialects.
 * Red is a signal color; neutral surfaces carry the majority of the UI.
 */

/* Semantic color roles (0xRRGGBB). */
#define KU_FLUX_COLOR_VOID_BLACK       UINT32_C(0x040507)
#define KU_FLUX_COLOR_BACKGROUND       UINT32_C(0x090A0C)
#define KU_FLUX_COLOR_SURFACE          UINT32_C(0x121316)
#define KU_FLUX_COLOR_SURFACE_ELEVATED UINT32_C(0x1F2126)
#define KU_FLUX_COLOR_SURFACE_HOVER    UINT32_C(0x221E23)
#define KU_FLUX_COLOR_SURFACE_PRESSED  UINT32_C(0x42121C)
#define KU_FLUX_COLOR_BORDER           UINT32_C(0x373A40)
#define KU_FLUX_COLOR_BORDER_FOCUS     UINT32_C(0xFF2230)
#define KU_FLUX_COLOR_TEXT_PRIMARY     UINT32_C(0xEEEFF2)
#define KU_FLUX_COLOR_TEXT_SECONDARY   UINT32_C(0x91959C)
#define KU_FLUX_COLOR_ACCENT           UINT32_C(0xDC1628)
#define KU_FLUX_COLOR_ACCENT_MUTED     UINT32_C(0x8B1824)
#define KU_FLUX_COLOR_ACCENT_DEEP      UINT32_C(0x530A14)
#define KU_FLUX_COLOR_DANGER           UINT32_C(0xFF3642)
#define KU_FLUX_COLOR_WARNING          UINT32_C(0xD88A32)
#define KU_FLUX_COLOR_INACTIVE         UINT32_C(0x2B2D32)
#define KU_FLUX_COLOR_STEEL            UINT32_C(0x555960)
#define KU_FLUX_COLOR_SHADOW           UINT32_C(0x010203)

/* The only general-purpose spacing steps used by new Flux UI 2.0 layouts. */
enum ku_flux_spacing_token {
    KU_FLUX_SPACE_1 = 4,
    KU_FLUX_SPACE_2 = 8,
    KU_FLUX_SPACE_3 = 12,
    KU_FLUX_SPACE_4 = 16,
    KU_FLUX_SPACE_6 = 24
};

/* Stable component/layout metrics for native retained UI packets. */
enum ku_flux_layout_token {
    KU_FLUX_PANEL_PADDING = KU_FLUX_SPACE_4,
    KU_FLUX_CARD_PADDING = KU_FLUX_SPACE_3,
    KU_FLUX_WINDOW_MARGIN = KU_FLUX_SPACE_3,
    KU_FLUX_TITLEBAR_HEIGHT = 36,
    KU_FLUX_MINIMUM_TARGET = 34,
    KU_FLUX_PANEL_HEIGHT = 38,
    KU_FLUX_LABEL_HEIGHT = 22,
    KU_FLUX_BUTTON_HEIGHT = 34,
    KU_FLUX_INPUT_HEIGHT = 36,
    KU_FLUX_LIST_ROW_HEIGHT = 36,
    KU_FLUX_PROGRESS_HEIGHT = 44,
    KU_FLUX_SEPARATOR_HEIGHT = 10,
    KU_FLUX_TILE_WIDTH = 184,
    KU_FLUX_TILE_HEIGHT = 68,
    KU_FLUX_METRIC_WIDTH = 112,
    KU_FLUX_METRIC_HEIGHT = 58
};

/* Roles are independent from a particular font engine. */
enum ku_flux_typography_role {
    KU_FLUX_TYPE_DISPLAY = 0,
    KU_FLUX_TYPE_TITLE,
    KU_FLUX_TYPE_SECTION,
    KU_FLUX_TYPE_BODY,
    KU_FLUX_TYPE_CAPTION,
    KU_FLUX_TYPE_STATUS,
    KU_FLUX_TYPE_MONOSPACE_DIAGNOSTIC,
    KU_FLUX_TYPE_ROLE_COUNT
};

#define KU_FLUX_TYPE_DISPLAY_SCALE UINT32_C(3)
#define KU_FLUX_TYPE_TITLE_SCALE   UINT32_C(2)
#define KU_FLUX_TYPE_TEXT_SCALE    UINT32_C(1)


/*
 * 8.0 desktop palettes: pure shared C/C++ ABI with stable IDs.
 * Semantic colors, never a global mutable palette in Ring-3.
 */
enum ku_flux_style_id { KU_FLUX_STYLE_CLASSIC = 0, KU_FLUX_STYLE_MODERN = 1, KU_FLUX_STYLE_COUNT = 2 };

enum ku_flux_theme_id {
    KU_FLUX_THEME_RED = 0,
    KU_FLUX_THEME_VIOLET = 1,
    KU_FLUX_THEME_CRIMSON = 2,
    KU_FLUX_THEME_STEEL = 3,
    KU_FLUX_THEME_EMBER = 4,
    KU_FLUX_THEME_MONO = 5,
    KU_FLUX_THEME_COUNT = 6
};
typedef struct ku_flux_palette {
    uint32_t desktop;
    uint32_t panel;
    uint32_t background;
    uint32_t border;
    uint32_t text;
    uint32_t text_muted;
    uint32_t accent;
    uint32_t accent_focus;
    uint32_t accent_deep;
    uint32_t accent_muted;
    uint32_t danger;
} ku_flux_palette;

static inline ku_flux_palette ku_flux_theme_palette(uint32_t theme_id) {
    static const ku_flux_palette presets[KU_FLUX_THEME_COUNT] = {
        {0x040507U,0x121316U,0x090A0CU,0x373A40U,0xEEEFF2U,0x91959CU,0xDC1628U,0xFF2230U,0x530A14U,0x8B1824U,0xFF3642U},
        {0x07050EU,0x191522U,0x0D0A16U,0x42374FU,0xF3EFFAU,0xB5A7C8U,0xA46DFFU,0xBE96FFU,0x34204BU,0x6C46A1U,0xFF5267U},
        {0x080507U,0x1A1215U,0x10090DU,0x543238U,0xF5EBEFU,0xB7A0A7U,0xBD2849U,0xEE4A68U,0x4D1123U,0x852641U,0xFF4869U},
        {0x070A0FU,0x151C24U,0x0B1118U,0x364858U,0xEBF3F9U,0x9DABB7U,0x6798C7U,0x89B9E0U,0x213C52U,0x486D8BU,0xF07179U},
        {0x0D0804U,0x201711U,0x140E09U,0x52402EU,0xF9F0E5U,0xC0AD96U,0xE89034U,0xFFB95FU,0x543010U,0x996022U,0xFF5D4AU},
        {0x08090AU,0x17191BU,0x101214U,0x474B50U,0xF2F2F2U,0xB3B6BAU,0xCDD1D6U,0xFFFFFFU,0x383B40U,0x82868CU,0xF37581U}
    };
    return presets[theme_id < KU_FLUX_THEME_COUNT ? theme_id : KU_FLUX_THEME_RED];
}
static inline const char* ku_flux_theme_name(uint32_t theme_id) {
    static const char* const names[KU_FLUX_THEME_COUNT] = {
        "KUROGANE RED", "KUROGANE VIOLET", "KUROGANE CRIMSON",
        "KUROGANE STEEL", "KUROGANE EMBER", "KUROGANE MONO"
    };
    return names[theme_id < KU_FLUX_THEME_COUNT ? theme_id : KU_FLUX_THEME_RED];
}

#endif
