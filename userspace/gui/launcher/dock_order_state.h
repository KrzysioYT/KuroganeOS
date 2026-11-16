#ifndef KUROGANE_FLUX_DOCK_ORDER_STATE_H
#define KUROGANE_FLUX_DOCK_ORDER_STATE_H

#include <stddef.h>
#include <stdint.h>

/* desktop.cfg v1 stores one pin byte; v2 adds these eight order bytes.
 * The reserved HOME app is never moved and a malformed permutation must
 * not be applied to a live desktop. */
#define FLUX_DOCK_ORDER_TAG UINT8_C(0xD8)
#define FLUX_DOCK_ORDER_BYTES 8U
#define FLUX_DOCK_RECORD_BYTES 13U

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

/* New desktop.cfg records carry a little-endian FNV-1a digest over the
 * pin mask and tagged order. The old 1-byte and 9-byte formats remain
 * readable, but every subsequent successful save publishes 13 bytes. */
static inline uint32_t flux_dock_record_checksum(const uint8_t record[FLUX_DOCK_RECORD_BYTES]) {
    uint32_t hash = UINT32_C(2166136261);
    if (record == NULL) return 0U;
    for (size_t index = 0U; index < 9U; ++index) {
        hash ^= (uint32_t)record[index];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static inline void flux_dock_record_seal(uint8_t record[FLUX_DOCK_RECORD_BYTES]) {
    if (record == NULL) return;
    const uint32_t checksum = flux_dock_record_checksum(record);
    for (size_t index = 0U; index < 4U; ++index)
        record[9U + index] = (uint8_t)(checksum >> (8U * index));
}

static inline int flux_dock_record_valid(const uint8_t record[FLUX_DOCK_RECORD_BYTES]) {
    if (record == NULL || (record[0] & UINT8_C(1)) == 0U ||
        !flux_dock_order_valid(record + 1U)) return 0;
    const uint32_t checksum = flux_dock_record_checksum(record);
    for (size_t index = 0U; index < 4U; ++index) {
        if (record[9U + index] != (uint8_t)(checksum >> (8U * index)))
            return 0;
    }
    return 1;
}

#endif
