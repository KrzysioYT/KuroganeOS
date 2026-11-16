#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../userspace/gui/launcher/dock_order_state.h"

int main(void) {
    const uint8_t original[FLUX_DOCK_ORDER_BYTES] = {
        FLUX_DOCK_ORDER_TAG, 1, 2, 3, 4, 5, 6, 7
    };
    uint8_t order[FLUX_DOCK_ORDER_BYTES];
    assert(flux_dock_order_valid(original));
    const uint8_t customized[FLUX_DOCK_ORDER_BYTES] = {
        FLUX_DOCK_ORDER_TAG, 2, 6, 1, 4, 3, 5, 7
    };
    assert(flux_dock_order_valid(customized));
    memcpy(order, customized, sizeof(order));
    order[0] = 0;
    assert(!flux_dock_order_valid(order));
    memcpy(order, customized, sizeof(order));
    order[1] = 0;
    assert(!flux_dock_order_valid(order));
    memcpy(order, customized, sizeof(order));
    order[2] = 8;
    assert(!flux_dock_order_valid(order));
    memcpy(order, customized, sizeof(order));
    order[2] = order[1];
    assert(!flux_dock_order_valid(order));
    assert(!flux_dock_order_valid(NULL));
    uint8_t record[FLUX_DOCK_RECORD_BYTES] = {
        0x07, FLUX_DOCK_ORDER_TAG, 2, 1, 3, 4, 5, 6, 7
    };
    flux_dock_record_seal(record);
    assert(flux_dock_record_valid(record));
    record[0] ^= UINT8_C(4);
    assert(!flux_dock_record_valid(record));
    record[0] ^= UINT8_C(4);
    record[12] ^= UINT8_C(1);
    assert(!flux_dock_record_valid(record));
    flux_dock_record_seal(record);
    assert(flux_dock_record_valid(record));
    return 0;
}
