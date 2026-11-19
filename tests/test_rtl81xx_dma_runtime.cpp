#include "../kernel/net/rtl81xx.cpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>

namespace {
uint32_t pci_identity = UINT32_C(0x816810EC);
uint16_t pci_command = 6U;
}
namespace pci {
uint32_t read32(Address, uint8_t offset) {
    assert(offset == 0U);
    return pci_identity;
}
uint16_t read16(Address, uint8_t offset) {
    assert(offset == 4U);
    return pci_command;
}
}

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
    g_device.pci_device.vendor_id = 0x10ECU;
    g_device.pci_device.device_id = 0x8168U;
    g_device.registers = mmio;
    g_device.started = true;
    g_device.tx_ring_page.virtual_address = tx_descriptors;
    g_device.rx_ring_page.virtual_address = rx_descriptors;
    for (size_t i = 0; i < DESCRIPTOR_COUNT; ++i) {
        g_device.tx_buffers[i].virtual_address = tx_data[i];
        g_device.rx_buffers[i].virtual_address = rx_data[i];
        g_device.tx_buffers[i].physical_address = UINT64_C(0x100000) + i * 4096U;
        g_device.rx_buffers[i].physical_address = UINT64_C(0x200000) + i * 4096U;
        tx_descriptors[i].address = g_device.tx_buffers[i].physical_address;
        rx_descriptors[i].address = g_device.rx_buffers[i].physical_address;
        tx_descriptors[i].flags =
            i + 1U == DESCRIPTOR_COUNT ? dma::END_OF_RING : 0U;
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
    const struct { uint32_t identity; uint16_t command; } unavailable_pci[] = {
        {0x816810ECU, 0U}, {0x816810ECU, 2U}, {0x816810ECU, 4U},
        {0x816810ECU, UINT16_MAX}, {UINT32_MAX, 6U},
        {0x81681234U, 6U}, {0x816910ECU, 6U},
    };
    for (auto state : unavailable_pci) {
        pci_identity = state.identity;
        pci_command = state.command;
        // An inaccessible BAR must never be touched after PCI validation fails.
        g_device.registers = reinterpret_cast<volatile uint8_t*>(1U);
        uint8_t output[128];
        std::memset(output, 0x5AU, sizeof(output));
        size_t copied = 99U;
        assert(!ready() && interface() == nullptr);
        assert(transmit(&g_device, short_frame, sizeof(short_frame)) ==
               net::Status::InterfaceError);
        assert(receive(&g_device, output, sizeof(output), &copied) ==
               net::Status::InterfaceError);
        assert(tx_descriptors[0].flags == 0U && g_device.next_tx == 0U);
        assert(g_device.next_rx == 0U && copied == 0U);
        assert(rx_descriptors[0].flags == dma::rx_rearm(false, dma::RX_BUFFER_BYTES));
        assert(g_device.rx_frames == 0U && g_device.tx_frames == 0U);
        for (uint8_t byte : output) assert(byte == 0x5AU);
    }
    pci_identity = 0x816810ECU;
    pci_command = 6U;
    g_device.registers = mmio;
    std::puts("RTL81xx PCI removal, identity and DMA decode loss reject I/O before MMIO: PASS");
    const uint8_t unavailable_commands[] = {
        0U, CMD_RX, CMD_TX, CMD_RESET,
        static_cast<uint8_t>(CMD_RESET | CMD_RX | CMD_TX), UINT8_MAX,
    };
    const uint32_t completed_rx = dma::FIRST_SEGMENT | dma::LAST_SEGMENT | 68U;
    rx_descriptors[0].flags = completed_rx;
    for (uint8_t command : unavailable_commands) {
        mmio[REG_CHIP_CMD] = command;
        uint8_t blocked_output[128];
        std::memset(blocked_output, 0x5AU, sizeof(blocked_output));
        size_t copied = 99U;
        assert(!ready() && interface() == nullptr);
        assert(transmit(&g_device, short_frame, sizeof(short_frame)) ==
               net::Status::InterfaceError);
        assert(receive(&g_device, blocked_output, sizeof(blocked_output), &copied) ==
               net::Status::InterfaceError);
        assert(tx_descriptors[0].flags == 0U && rx_descriptors[0].flags == completed_rx);
        assert(g_device.next_rx == 0U && g_device.next_tx == 0U);
        assert(g_device.rx_frames == 0U && g_device.tx_frames == 0U);
        assert(mmio[REG_TX_POLL] == 0U && tx_data[0][0] == 0U && copied == 0U);
        for (uint8_t byte : blocked_output) assert(byte == 0x5AU);
    }
    mmio[REG_CHIP_CMD] = CMD_RX | CMD_TX;
    assert(ready() && interface() != nullptr);
    rx_descriptors[0].flags = dma::rx_rearm(false, dma::RX_BUFFER_BYTES);
    std::puts("RTL81xx stopped/reset DMA engines reject TX/RX without descriptor mutation: PASS");

    // Inject a corrupted physical address and a missing wrap marker on the
    // actual production TX/RX paths. No buffer copy, descriptor publication,
    // ring advance or TX doorbell is permitted. Quarantine survives a second
    // initialize() call; tests alone can clear it between independent cases.
    for (unsigned fault = 0U; fault < 4U; ++fault) {
        const bool receive_fault = fault >= 2U;
        auto& slot = receive_fault ? rx_descriptors[0] : tx_descriptors[0];
        const uint32_t before_flags = slot.flags;
        const uint64_t before_address = slot.address;
        const size_t before_tx = g_device.next_tx, before_rx = g_device.next_rx;
        const uint64_t before_tx_frames = g_device.tx_frames;
        const uint64_t before_rx_frames = g_device.rx_frames;
        const uint8_t old_doorbell = mmio[REG_TX_POLL];
        if ((fault & 1U) == 0U) slot.address ^= UINT64_C(8);
        else slot.flags ^= dma::END_OF_RING;

        uint8_t blocked[128];
        std::memset(blocked, 0xA5, sizeof(blocked));
        size_t copied = 99U;
        if (receive_fault) {
            assert(receive(&g_device, blocked, sizeof(blocked), &copied) ==
                   net::Status::InterfaceError);
            assert(copied == 0U);
        } else {
            assert(transmit(&g_device, short_frame, sizeof(short_frame)) ==
                   net::Status::InterfaceError);
        }
        assert(g_device.faulted && last_status() == Status::DeviceFault);
        assert(!ready() && interface() == nullptr);
        assert(initialize() == Status::DeviceFault);
        assert(g_device.next_tx == before_tx && g_device.next_rx == before_rx);
        assert(g_device.tx_frames == before_tx_frames &&
               g_device.rx_frames == before_rx_frames);
        assert(mmio[REG_TX_POLL] == old_doorbell);
        assert(slot.address == ((fault & 1U) == 0U
            ? (before_address ^ UINT64_C(8)) : before_address));
        assert(slot.flags == ((fault & 1U) != 0U
            ? (before_flags ^ dma::END_OF_RING) : before_flags));
        for (uint8_t byte : blocked) assert(byte == 0xA5);
        slot.address = before_address;
        slot.flags = before_flags;
        g_device.faulted = false; // Independent synthetic test case only.
        g_status = Status::Ok;
        assert(ready());
    }
    std::puts("RTL81xx corrupt RX/TX DMA bindings quarantine device without mutation: PASS");
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

    /* Carrier loss and vanished MMIO must not copy or consume stale DMA
     * payload. Restoring link must allow the same descriptor to be read. */
    mmio[REG_PHY_STATUS] = 0U;
    assert(receive(&g_device, output, sizeof(output), &copied) ==
           net::Status::InterfaceError);
    assert(copied == 0U && g_device.next_rx == 0U);
    assert((rx_descriptors[0].flags & dma::OWN) == 0U);
    mmio[REG_PHY_STATUS] = UINT8_MAX;
    assert(receive(&g_device, output, sizeof(output), &copied) ==
           net::Status::InterfaceError);
    assert(g_device.next_rx == 0U);
    mmio[REG_PHY_STATUS] = PHY_LINK_UP;
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
