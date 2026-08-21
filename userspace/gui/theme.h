#ifndef KUROGANE_GUI_THEME_H
#define KUROGANE_GUI_THEME_H

#include <stdint.h>
#include <kurogane/libui.h>

/*
 * KuroganeOS 5 "Obsidian" desktop palette.
 *
 * The current Ring-3 scene transport is still text compatible, but keeping
 * the visual language in one place lets native widgets/compositor surfaces
 * consume exactly the same tokens later.  Applications should not hard-code
 * their own red/graphite variants anymore.
 */
#define KU_GUI_COLOR_OBSIDIAN       UINT32_C(0x0B0D10)
#define KU_GUI_COLOR_SURFACE        UINT32_C(0x15181D)
#define KU_GUI_COLOR_SURFACE_RAISED UINT32_C(0x1B1F25)
#define KU_GUI_COLOR_STEEL          UINT32_C(0x2D323A)
#define KU_GUI_COLOR_TEXT           UINT32_C(0xF1F3F5)
#define KU_GUI_COLOR_TEXT_MUTED     UINT32_C(0x9AA1AA)
#define KU_GUI_COLOR_CRIMSON        UINT32_C(0xE32636)
#define KU_GUI_COLOR_CRIMSON_MUTED  UINT32_C(0xA92835)
#define KU_GUI_COLOR_SUCCESS        UINT32_C(0x39C97A)
#define KU_GUI_COLOR_WARNING        UINT32_C(0xD6A84A)

static inline void gui_apply_obsidian_theme(kui_scene* scene, int subdued) {
    if (scene == (kui_scene*)0) return;
    kui_scene_set_palette(
        scene,
        KU_GUI_COLOR_OBSIDIAN,
        KU_GUI_COLOR_TEXT,
        subdued != 0 ? KU_GUI_COLOR_CRIMSON_MUTED : KU_GUI_COLOR_CRIMSON);
}

#endif
