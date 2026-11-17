#include "../kernel/net/rtl81xx_ring.hpp"
#include <cassert>
#include <cstdint>
#include <cstddef>

int main() {
    using namespace net::rtl81xx::dma;
    static_assert(sizeof(Descriptor) == 16U);
    static_assert(alignof(Descriptor) == 16U);
    static_assert(rx_rearm(false, 2048U) == (OWN | 2048U));
    static_assert(rx_rearm(true, 2048U) == (OWN | END_OF_RING | 2048U));
    static_assert(rx_rearm(true, 2049U) == 0U);
    static_assert(tx_submit(false, 1514U) == (OWN | FIRST_SEGMENT |
                  LAST_SEGMENT | 1514U));
    static_assert(tx_submit(true, 14U) == (OWN | END_OF_RING |
                  FIRST_SEGMENT | LAST_SEGMENT | 14U));
    static_assert(tx_submit(false, 0U) == 0U);
    static_assert(tx_submit(false, 1515U) == 0U);
    static_assert(valid_dma_buffer(UINT64_C(0x1000), 2048U));
    static_assert(!valid_dma_buffer(UINT64_C(0x1001), 2048U));
    static_assert(!valid_dma_buffer(UINT64_C(0xFFFFFFFFFFFFFFF8), 32U));
    static_assert(!valid_dma_buffer(UINT64_C(0x1000), 0U));
    assert(inspect_rx(OWN | 64U, 1514U).result == Receive::NotReady);
    assert(inspect_rx(FIRST_SEGMENT | LAST_SEGMENT | (64U + 4U), 1514U).result ==
           Receive::Ready);
    assert(inspect_rx(FIRST_SEGMENT | LAST_SEGMENT | (1514U + 4U), 1514U).payload_length ==
           1514U);
    assert(inspect_rx(FIRST_SEGMENT | LAST_SEGMENT | (1514U + 4U), 100U).result ==
           Receive::OutputTooSmall);
    assert(inspect_rx(FIRST_SEGMENT | LAST_SEGMENT | (1514U + 4U), 100U).payload_length ==
           1514U);
    assert(inspect_rx(FIRST_SEGMENT | LAST_SEGMENT | RX_ERROR | 68U, 1514U).result ==
           Receive::InvalidFrame);
    assert(inspect_rx(FIRST_SEGMENT | 68U, 1514U).result == Receive::InvalidFrame);
    assert(inspect_rx(LAST_SEGMENT | 68U, 1514U).result == Receive::InvalidFrame);
    assert(inspect_rx(FIRST_SEGMENT | LAST_SEGMENT | 3U, 1514U).result ==
           Receive::InvalidFrame);
    assert(inspect_rx(FIRST_SEGMENT | LAST_SEGMENT | 2049U, 1514U).result ==
           Receive::InvalidFrame);
    return 0;
}
