#ifndef KUROGANE_FLUX_DOCK_ORDER_STATE_H
#define KUROGANE_FLUX_DOCK_ORDER_STATE_H

#include <stddef.h>
#include <stdint.h>

/* desktop.cfg v1 stores one pin byte; v2 adds these eight order bytes.
 * The reserved HOME app is never moved and a malformed permutation must
 * not be applied to a live desktop. */
#define FLUX_DOCK_ORDER_TAG UINT8_C(0xD8)
#define FLUX_DOCK_ORDER_BYTES 8U

static inline int flux_dock_order_valid(const uint8_t order[FLUX_DOCK_ORDER_BYTES]) {
    uint8_t seen = UINT8_C(1);
    if (order == NULL || order[0] != FLUX_DOCK_ORDER_TAG) return 0;
    for (size_t position = 1U; position < FLUX_DOCK_ORDER_BYTES; ++position) {
        const uint8_t app_id = order[position];
        if (app_id == 0U || app_id >= FLUX_DOCK_ORDER_BYTES ||
            (seen & (uint8_t)(UINT8_C(1) << app_id)) != 0U) return 0;
        seen = (uint8_t)(seen | (uint8_t)(UINT8_C(1) << app_id));
    }
    return seen == UINT8_MAX;
}

#endif
