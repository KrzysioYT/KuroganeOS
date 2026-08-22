#ifndef KUROGANE_GUI_THEME_H
#define KUROGANE_GUI_THEME_H

#include <stdint.h>
#include <kurogane/libui.h>

/*
 * KuroganeOS 5 "Forged Steel" design tokens.
 *
 * Source of truth: the final KuroganeOS 5 design board (Blade / Vault /
 * Anvil / Forge Control / Pulse). Keep these tokens centralized so the
 * compatibility scene renderer and the future native compositor share one
 * identity.
 */
#define KU_GUI_COLOR_OBSIDIAN       UINT32_C(0x090E0E)
#define KU_GUI_COLOR_FORGED_STEEL   UINT32_C(0x171C22)
#define KU_GUI_COLOR_STEEL_RAISED   UINT32_C(0x20262D)
#define KU_GUI_COLOR_ASH            UINT32_C(0xA8AFB8)
#define KU_GUI_COLOR_TEXT           UINT32_C(0xEEF1F4)
#define KU_GUI_COLOR_TEXT_MUTED     UINT32_C(0x89919A)
#define KU_GUI_COLOR_CRIMSON        UINT32_C(0xE62932)
#define KU_GUI_COLOR_HOT_EDGE       UINT32_C(0xFF4A45)
#define KU_GUI_COLOR_SUCCESS        UINT32_C(0x56C98A)
#define KU_GUI_COLOR_WARNING        UINT32_C(0xD9A441)

#define KU_GUI_BRAND_TAGLINE "BUILT IN STEEL. REFINED IN FIRE."
#define KU_GUI_SHELL_BLADE "BLADE"
#define KU_GUI_APP_KUROSH "KUROSH"
#define KU_GUI_APP_VAULT "VAULT"
#define KU_GUI_APP_ANVIL "ANVIL"
#define KU_GUI_APP_FORGE "FORGE CONTROL"
#define KU_GUI_PANEL_PULSE "PULSE"

static inline void gui_apply_forged_theme(kui_scene* scene, int hot_edge) {
    if (scene == (kui_scene*)0) return;
    kui_scene_set_palette(
        scene,
        KU_GUI_COLOR_OBSIDIAN,
        KU_GUI_COLOR_TEXT,
        hot_edge != 0 ? KU_GUI_COLOR_HOT_EDGE : KU_GUI_COLOR_CRIMSON);
}

/* Compatibility alias while the 5.0 branch migrates every application. */
static inline void gui_apply_obsidian_theme(kui_scene* scene, int subdued) {
    gui_apply_forged_theme(scene, subdued != 0);
}

#endif
