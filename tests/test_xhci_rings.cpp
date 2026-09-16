#include <cassert>
#include <cstdio>

#include "../kernel/drivers/usb/xhci.cpp"

int main() {
    using namespace drivers::usb::xhci;
    alignas(4096) Trb producer_entries[RING_TRB_COUNT]{};
    ProducerRing producer{};
    producer.page = {producer_entries, UINT64_C(0x12345000), true};
    producer.cycle = true;
    producer_entries[USABLE_RING_TRBS] = {
        producer.page.physical_address, 0U,
        static_cast<uint32_t>(TRB_LINK) << 10U | 3U,
    };

    for (size_t sequence = 0U; sequence < USABLE_RING_TRBS * 8U; ++sequence) {
        const size_t index = sequence % USABLE_RING_TRBS;
        const bool cycle = ((sequence / USABLE_RING_TRBS) % 2U) == 0U;
        const uint64_t address = enqueue_trb(
            producer, sequence + 1U, static_cast<uint32_t>(sequence),
            static_cast<uint32_t>(TRB_NORMAL) << 10U);
        assert(address == producer.page.physical_address + index * sizeof(Trb));
        assert(producer_entries[index].parameter == sequence + 1U);
        assert(producer_entries[index].status == sequence);
        assert((producer_entries[index].control & 1U) == (cycle ? 1U : 0U));
        assert(producer.enqueue == index + 1U && producer.cycle == cycle);
        const Trb& link = producer_entries[USABLE_RING_TRBS];
        assert(trb_type(link) == TRB_LINK);
        assert(link.parameter == producer.page.physical_address);
        assert((link.control & 2U) != 0U); // Toggle-cycle bit survives reuse.
        if (sequence != 0U && index == 0U) {
            assert((link.control & 1U) == (cycle ? 0U : 1U));
        }
    }

    alignas(4096) Trb event_entries[RING_TRB_COUNT]{};
    alignas(8) uint8_t runtime[0x40]{};
    Controller consumer{};
    consumer.event_ring_page = {event_entries, UINT64_C(0x22345000), true};
    consumer.runtime = runtime;
    consumer.event_cycle = true;
    Trb output{UINT64_MAX, UINT32_MAX, UINT32_MAX};
    assert(!next_event(consumer, &output));
    assert(output.parameter == UINT64_MAX && consumer.event_dequeue == 0U);

    for (size_t sequence = 0U; sequence < RING_TRB_COUNT * 8U; ++sequence) {
        const size_t index = sequence % RING_TRB_COUNT;
        const bool cycle = ((sequence / RING_TRB_COUNT) % 2U) == 0U;
        // Empty/stale slots must not move the dequeue or publish an ERDP.
        assert(!next_event(consumer, &output));
        assert(consumer.event_dequeue == index && consumer.event_cycle == cycle);
        event_entries[index] = {
            sequence + 1U, static_cast<uint32_t>(sequence),
            static_cast<uint32_t>(TRB_TRANSFER_EVENT) << 10U | (cycle ? 1U : 0U),
        };
        assert(next_event(consumer, &output));
        assert(output.parameter == sequence + 1U && output.status == sequence);
        assert(trb_type(output) == TRB_TRANSFER_EVENT);
        const size_t next = (index + 1U) % RING_TRB_COUNT;
        assert(consumer.event_dequeue == next);
        assert(consumer.event_cycle == (next == 0U ? !cycle : cycle));
        const uint64_t erdp = static_cast<uint64_t>(read32(runtime, 0x38U)) |
            static_cast<uint64_t>(read32(runtime, 0x3CU)) << 32U;
        assert(erdp == ((consumer.event_ring_page.physical_address +
                        next * sizeof(Trb)) | UINT64_C(8)));
    }
    assert(!next_event(consumer, &output));
    std::puts("xHCI producer/event ring cycle and eight-wrap regression: PASS");
    return 0;
}
