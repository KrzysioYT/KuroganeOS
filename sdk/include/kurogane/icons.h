#ifndef KUROGANE_SDK_ICONS_H
#define KUROGANE_SDK_ICONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t ku_icon_id_t;

enum ku_icon_category {
    KU_ICON_CATEGORY_APPLICATION = 1,
    KU_ICON_CATEGORY_FOLDER = 2,
    KU_ICON_CATEGORY_FILE_TYPE = 3,
    KU_ICON_CATEGORY_DEVICE = 4,
    KU_ICON_CATEGORY_STATUS = 5,
    KU_ICON_CATEGORY_ACTION = 6,
    KU_ICON_CATEGORY_NAVIGATION = 7,
    KU_ICON_CATEGORY_WIDGET = 8,
    KU_ICON_CATEGORY_CURSOR = 9,
    KU_ICON_CATEGORY_SPECIAL = 10,
    KU_ICON_CATEGORY_BRANDING = 11,
    KU_ICON_CATEGORY_MICRO = 12,
    KU_ICON_CATEGORY_KUROGANE_APP = 13
};

#define KU_ICON_NONE ((ku_icon_id_t)UINT16_C(0))
#define KU_ICON_CATEGORY_OF(icon_id) ((uint8_t)((uint16_t)(icon_id) >> 8U))
#define KU_ICON_ORDINAL_OF(icon_id) ((uint8_t)((uint16_t)(icon_id) & UINT16_C(0xFF)))

#include <kurogane/icons.generated.h>

#ifdef __cplusplus
}
#endif

#endif
