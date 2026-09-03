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

#endif
