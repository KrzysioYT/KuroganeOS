#ifndef KUROGANE_SDK_TEXT_H
#define KUROGANE_SDK_TEXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KU_TEXT_ABI_VERSION UINT32_C(1)
#define KU_TEXT_MIN_SIZE_PX UINT32_C(6)
#define KU_TEXT_MAX_SIZE_PX UINT32_C(256)
#define KU_TEXT_WEIGHT_NORMAL UINT32_C(400)
#define KU_TEXT_WEIGHT_MEDIUM UINT32_C(500)
#define KU_TEXT_WEIGHT_SEMIBOLD UINT32_C(600)
#define KU_TEXT_WEIGHT_BOLD UINT32_C(700)

/*
 * Logical families are deliberately independent from concrete font files.
 * Font discovery resolves them later.  In particular, SYSTEM_UI is not a
 * browser-wide override: document renderers should use it only when page CSS
 * explicitly requests the CSS generic family `system-ui`.
 */
enum ku_font_family {
    KU_FONT_FAMILY_SYSTEM_UI = 1,
    KU_FONT_FAMILY_SANS_SERIF = 2,
    KU_FONT_FAMILY_SERIF = 3,
    KU_FONT_FAMILY_MONOSPACE = 4
};

enum ku_font_slant {
    KU_FONT_SLANT_NORMAL = 0,
    KU_FONT_SLANT_ITALIC = 1
};

enum ku_text_context_kind {
    KU_TEXT_CONTEXT_SYSTEM_UI = 1,
    KU_TEXT_CONTEXT_DOCUMENT = 2
};

typedef struct ku_text_style {
    uint32_t structure_size;
    uint32_t family;
    uint32_t weight;
    uint32_t slant;
    uint32_t size_px;
    int32_t letter_spacing_px;
    uint32_t line_height_px;
    uint32_t reserved;
} ku_text_style;

static inline int ku_font_family_valid(uint32_t family) {
    return family >= KU_FONT_FAMILY_SYSTEM_UI &&
        family <= KU_FONT_FAMILY_MONOSPACE;
}

static inline int ku_font_weight_valid(uint32_t weight) {
    return weight >= UINT32_C(100) && weight <= UINT32_C(900) &&
        (weight % UINT32_C(100)) == 0U;
}

static inline int ku_text_style_valid(const ku_text_style* style) {
    return style != NULL &&
        style->structure_size == sizeof(*style) &&
        ku_font_family_valid(style->family) &&
        ku_font_weight_valid(style->weight) &&
        (style->slant == KU_FONT_SLANT_NORMAL ||
         style->slant == KU_FONT_SLANT_ITALIC) &&
        style->size_px >= KU_TEXT_MIN_SIZE_PX &&
        style->size_px <= KU_TEXT_MAX_SIZE_PX &&
        (style->line_height_px == 0U ||
         style->line_height_px >= style->size_px) &&
        style->reserved == 0U;
}

static inline ku_text_style ku_text_default_style(uint32_t context) {
    ku_text_style style;
    style.structure_size = sizeof(style);
    style.family = context == KU_TEXT_CONTEXT_DOCUMENT
        ? KU_FONT_FAMILY_SANS_SERIF
        : KU_FONT_FAMILY_SYSTEM_UI;
    style.weight = KU_TEXT_WEIGHT_NORMAL;
    style.slant = KU_FONT_SLANT_NORMAL;
    style.size_px = UINT32_C(14);
    style.letter_spacing_px = 0;
    style.line_height_px = UINT32_C(18);
    style.reserved = 0U;
    return style;
}

#ifdef __cplusplus
}
static_assert(sizeof(ku_text_style) == 32, "text style ABI mismatch");
#else
_Static_assert(sizeof(ku_text_style) == 32, "text style ABI mismatch");
#endif

#endif
