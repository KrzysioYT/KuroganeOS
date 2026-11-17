#include "../kernel/net/rtl81xx.cpp"

#include <cassert>
#include <cstdint>
#include <cstring>

/* Driver-runtime fixture: real production transmit()/receive() logic, fake
 * MMIO registers and DMA pages. This is NOT a hardware qualification. */
int main() {
    using namespace net::rtl81xx;
    uint8_t mmio[MMIO_BYTES]{};
    alignas(16) dma::Descriptor tx_descriptors[DESCRIPTOR_COUNT]{};
    alignas(16) dma::Descriptor rx_descriptors[DESCRIPTOR_COUNT]{};
    alignas(16) uint8_t tx_data[DESCRIPTOR_COUNT][dma::RX_BUFFER_BYTES]{};
    alignas(16) uint8_t rx_data[DESCRIPTOR_COUNT][dma::RX_BUFFER_BYTES]{};
    g_device = {};
    g_device.registers = mmio;
    g_device.started = true;
    g_device.tx_ring_page.virtual_address = tx_descriptors;
    g_device.rx_ring_page.virtual_address = rx_descriptors;
    for (size_t i = 0; i < DESCRIPTOR_COUNT; ++i) {
        g_device.tx_buffers[i].virtual_address = tx_data[i];
        g_device.rx_buffers[i].virtual_address = rx_data[i];
        rx_descriptors[i].flags = dma::rx_rearm(i == DESCRIPTOR_COUNT - 1U,
                                               dma::RX_BUFFER_BYTES);
    }

    uint8_t short_frame[dma::MIN_ETHERNET_FRAME]{};
    for (size_t i = 0; i < sizeof(short_frame); ++i)
        short_frame[i] = static_cast<uint8_t>(i + 1U);

    /* Link down or surprise removal must never queue a new DMA TX. */
    assert(transmit(&g_device, short_frame, sizeof(short_frame)) ==
           net::Status::InterfaceError);
    assert(tx_descriptors[0].flags == 0U);
    mmio[REG_PHY_STATUS] = UINT8_MAX;
    assert(transmit(&g_device, short_frame, sizeof(short_frame)) ==
           net::Status::InterfaceError);

    mmio[REG_PHY_STATUS] = PHY_LINK_UP;
    assert(transmit(&g_device, short_frame, sizeof(short_frame)) ==
           net::Status::Ok);
    assert((tx_descriptors[0].flags & dma::OWN) != 0U);
    assert((tx_descriptors[0].flags & dma::RX_LENGTH_MASK) == 60U);
    assert(mmio[REG_TX_POLL] == TX_POLL_NORMAL);
    assert(std::memcmp(tx_data[0], short_frame, sizeof(short_frame)) == 0);
    for (size_t i = sizeof(short_frame); i < 60U; ++i)
        assert(tx_data[0][i] == 0U);

    /* Fill the TX ring, require backpressure, then simulate completion. */
    for (size_t i = 1U; i < DESCRIPTOR_COUNT; ++i)
        assert(transmit(&g_device, short_frame, sizeof(short_frame)) ==
               net::Status::Ok);
    assert((tx_descriptors[DESCRIPTOR_COUNT - 1U].flags & dma::END_OF_RING) != 0U);
    assert(transmit(&g_device, short_frame, sizeof(short_frame)) ==
           net::Status::QueueFull);
    tx_descriptors[0].flags &= ~dma::OWN;
    assert(transmit(&g_device, short_frame, sizeof(short_frame)) ==
           net::Status::Ok);
    assert(g_device.tx_frames == DESCRIPTOR_COUNT + 1U);

    /* Actual RX decode path strips the Ethernet FCS, copies only payload and
     * rearms descriptor exactly once after successfully consuming it. */
    for (size_t i = 0; i < 64U; ++i)
        rx_data[0][i] = static_cast<uint8_t>(0x30U + i);
    rx_descriptors[0].flags =
        dma::FIRST_SEGMENT | dma::LAST_SEGMENT | 68U;
    uint8_t output[dma::MAX_ETHERNET_FRAME]{};
    size_t copied = 0U;
    assert(receive(&g_device, output, sizeof(output), &copied) ==
           net::Status::Ok);
    assert(copied == 64U);
    assert(std::memcmp(output, rx_data[0], copied) == 0);
    assert((rx_descriptors[0].flags & dma::OWN) != 0U);
    assert(g_device.next_rx == 1U);

    /* A short caller buffer must not consume/rearm the incoming frame.
     * A later retry with a suitable buffer can receive the same bytes. */
    rx_descriptors[1].flags =
        dma::FIRST_SEGMENT | dma::LAST_SEGMENT | 68U;
    rx_data[1][0] = 0xABU;
    copied = 0U;
    assert(receive(&g_device, output, 30U, &copied) ==
           net::Status::BufferTooSmall);
    assert(copied == 64U);
    assert(g_device.next_rx == 1U);
    assert((rx_descriptors[1].flags & dma::OWN) == 0U);
    assert(receive(&g_device, output, sizeof(output), &copied) ==
           net::Status::Ok);
    assert(copied == 64U && output[0] == 0xABU);

    /* Invalid or partial frames are dropped and the DMA buffer is returned
     * without copying untrusted contents into the caller's output. */
    rx_descriptors[2].flags = dma::FIRST_SEGMENT | dma::RX_ERROR | 68U;
    std::memset(output, 0x5AU, sizeof(output));
    assert(receive(&g_device, output, sizeof(output), &copied) ==
           net::Status::InterfaceError);
    assert(copied == 0U);
    assert(output[0] == 0x5AU);
    assert((rx_descriptors[2].flags & dma::OWN) != 0U);
    assert(g_device.dropped == 1U);
    return 0;
}
