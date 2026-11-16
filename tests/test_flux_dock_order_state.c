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
    return 0;
}
