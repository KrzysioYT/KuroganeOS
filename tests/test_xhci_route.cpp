#include "../kernel/drivers/usb/xhci_route.hpp"
#include <cassert>
#include <cstdint>

int main() {
    namespace route = drivers::usb::xhci::route;
    route::Address root{7U, 0U, 0U};
    assert(route::valid(root));
    uint32_t dw0 = 0U, dw1 = 0U;
    assert(route::slot_context_words(root, 4U, 1U, &dw0, &dw1));
    assert(dw0 == ((UINT32_C(4) << 20U) | (UINT32_C(1) << 27U)));
    assert(dw1 == (UINT32_C(7) << 16U));

    route::Address next{};
    assert(route::extend(root, 3U, &next));
    assert(next.root_port == 7U && next.tiers == 1U &&
           next.route_string == 0x3U);
    assert(route::slot_context_words(next, 3U, 2U, &dw0, &dw1));
    assert(dw0 == (UINT32_C(3) | (UINT32_C(3) << 20U) |
                   (UINT32_C(2) << 27U)));
    assert(dw1 == (UINT32_C(7) << 16U));
    const uint8_t ports[] = { 15U, 1U, 8U, 2U };
    for (const auto port : ports) {
        const auto before = next;
        assert(route::extend(next, port, &next));
        assert(next.tiers == before.tiers + 1U);
        assert((next.route_string >> (4U * before.tiers) & 0xFU) == port);
        assert(route::valid(next));
    }
    assert(next.tiers == 5U && next.route_string == 0x281F3U);

    const auto preserved = next;
    assert(!route::extend(next, 1U, &next));
    assert(next.route_string == preserved.route_string);
    assert(!route::extend(root, 0U, &next));
    assert(!route::extend(root, 16U, &next));
    assert(!route::extend(root, 1U, nullptr));
    assert(next.tiers == preserved.tiers);
    assert(!route::valid({0U, 0U, 0U}));
    assert(!route::valid({1U, 1U, 0U}));     // Missing first tier.
    assert(!route::valid({1U, 0U, 1U}));     // Unexpected non-zero route.
    assert(!route::valid({1U, 2U, 0x10U}));  // Tier gap.
    assert(!route::valid({1U, 6U, 0x11111U}));
    assert(!route::valid({1U, 5U, 0x100000U}));

    dw0 = UINT32_C(0xABCD);
    dw1 = UINT32_C(0xDCBA);
    assert(!route::slot_context_words({0U, 0U, 0U}, 4U, 1U, &dw0, &dw1));
    assert(!route::slot_context_words(root, 0U, 1U, &dw0, &dw1));
    assert(!route::slot_context_words(root, 16U, 1U, &dw0, &dw1));
    assert(!route::slot_context_words(root, 4U, 0U, &dw0, &dw1));
    assert(!route::slot_context_words(root, 4U, 32U, &dw0, &dw1));
    assert(!route::slot_context_words(root, 4U, 1U, nullptr, &dw1));
    assert(!route::slot_context_words(root, 4U, 1U, &dw0, &dw0));
    assert(dw0 == UINT32_C(0xABCD) && dw1 == UINT32_C(0xDCBA));
    return 0;
}
