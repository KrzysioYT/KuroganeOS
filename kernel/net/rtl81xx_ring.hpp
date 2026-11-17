#pragma once

#include <stddef.h>
#include <stdint.h>

namespace net::rtl81xx::dma {

/* RTL8168/RTL8111 legacy C+ descriptor format; no driver/hardware support
 * is implied by this bounded host-tested codec. All addresses are DMA
 * physical addresses, never CPU virtual pointers. */
struct alignas(16) Descriptor {
    uint32_t flags;
    uint32_t vlan;
    uint64_t address;
};
static_assert(sizeof(Descriptor) == 16U, "RTL81xx descriptor ABI");
static_assert(alignof(Descriptor) == 16U, "RTL81xx descriptor alignment");

constexpr uint32_t OWN = UINT32_C(1) << 31U;
constexpr uint32_t END_OF_RING = UINT32_C(1) << 30U;
constexpr uint32_t FIRST_SEGMENT = UINT32_C(1) << 29U;
constexpr uint32_t LAST_SEGMENT = UINT32_C(1) << 28U;
constexpr uint32_t RX_ERROR = UINT32_C(1) << 21U;
constexpr uint32_t RX_LENGTH_MASK = UINT32_C(0x3FFF);
constexpr size_t MAX_ETHERNET_FRAME = 1514U;
constexpr size_t MIN_ETHERNET_FRAME = 14U;
constexpr size_t RX_BUFFER_BYTES = 2048U;
constexpr size_t ETHERNET_FCS_BYTES = 4U;

constexpr bool valid_dma_buffer(uint64_t physical, size_t length) {
    return physical != 0U && (physical & UINT64_C(7)) == 0U &&
        length > 0U && length <= RX_BUFFER_BYTES &&
        physical <= UINT64_MAX - static_cast<uint64_t>(length - 1U);
}

constexpr uint32_t rx_rearm(bool last, size_t capacity) {
    return capacity != 0U && capacity <= RX_BUFFER_BYTES
        ? OWN | (last ? END_OF_RING : 0U) | static_cast<uint32_t>(capacity)
        : 0U;
}
constexpr uint32_t tx_submit(bool last, size_t length) {
    return length >= MIN_ETHERNET_FRAME && length <= MAX_ETHERNET_FRAME
        ? OWN | FIRST_SEGMENT | LAST_SEGMENT |
            (last ? END_OF_RING : 0U) | static_cast<uint32_t>(length)
        : 0U;
}

enum class Receive : uint8_t {
    NotReady,
    Ready,
    InvalidFrame,
    OutputTooSmall
};
struct ReceiveInfo {
    Receive result;
    size_t payload_length;
};
constexpr ReceiveInfo inspect_rx(uint32_t flags, size_t capacity) {
    if ((flags & OWN) != 0U) return {Receive::NotReady, 0U};
    const size_t wire_bytes = static_cast<size_t>(flags & RX_LENGTH_MASK);
    if ((flags & RX_ERROR) != 0U ||
        (flags & (FIRST_SEGMENT | LAST_SEGMENT)) !=
            (FIRST_SEGMENT | LAST_SEGMENT) ||
        wire_bytes < MIN_ETHERNET_FRAME + ETHERNET_FCS_BYTES ||
        wire_bytes > RX_BUFFER_BYTES)
        return {Receive::InvalidFrame, 0U};
    const size_t payload = wire_bytes - ETHERNET_FCS_BYTES;
    if (payload > MAX_ETHERNET_FRAME)
        return {Receive::InvalidFrame, 0U};
    if (payload > capacity)
        return {Receive::OutputTooSmall, payload};
    return {Receive::Ready, payload};
}
} // namespace net::rtl81xx::dma
