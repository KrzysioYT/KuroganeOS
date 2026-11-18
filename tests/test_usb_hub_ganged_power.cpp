#include "../kernel/drivers/usb/hub_port.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

namespace hub = drivers::usb::hub;
struct Fixture {
    hub::Descriptor descriptor{};
    hub::PortStatus before[hub::MAX_ROUTE_PORTS]{};
    hub::PortStatus after[hub::MAX_ROUTE_PORTS]{};
    size_t calls = 0U, waits = 0U, fail_at = SIZE_MAX;
    bool timer_ok = true;
    explicit Fixture(uint8_t count) {
        descriptor.port_count = count;
        descriptor.power_mode = hub::PowerMode::Ganged;
        for (auto& port : after) port = {hub::PORT_POWERED | hub::PORT_CONNECTED, hub::CHANGE_CONNECTION};
    }
    hub::PowerResult run() {
        auto exchange = [&](uint8_t port, hub::PortRequest request, hub::PortStatus* output) {
            const size_t n = descriptor.port_count;
            const size_t phase = calls / n;
            assert(phase < 3U && port == calls % n + 1U);
            assert(request == (phase == 1U ? hub::PortRequest::PowerOn : hub::PortRequest::GetStatus));
            assert((output == nullptr) == (phase == 1U));
            if (output) *output = phase == 0U ? before[port - 1U] : after[port - 1U];
            return calls++ != fail_at;
        };
        auto wait = [&](uint32_t us) {
            assert(calls == 2U * descriptor.port_count);
            assert(us == (descriptor.power_on_delay_2ms == 255U ? 510000U : 100000U));
            ++waits;
            return timer_ok;
        };
        return hub::power_ganged_hub_for_probe(descriptor, exchange, wait);
    }
};
int main() {
    for (uint8_t count : {uint8_t{1}, uint8_t{4}, hub::MAX_ROUTE_PORTS}) {
        for (uint8_t delay : {uint8_t{0}, uint8_t{255}}) {
            Fixture f(count); f.descriptor.power_on_delay_2ms = delay;
            assert(f.run() == hub::PowerResult::Ready);
            assert(f.calls == 3U * count && f.waits == 1U);
        }
        for (size_t failure = 0U; failure < 3U * count; ++failure) {
            Fixture f(count); f.fail_at = failure;
            assert(f.run() == hub::PowerResult::UnsafePort);
            assert(f.calls == failure + 1U && f.waits == (failure >= 2U * count ? 1U : 0U));
        }
    }
    for (uint16_t bad : {hub::PORT_POWERED, hub::PORT_ENABLED, hub::PORT_RESET,
                         hub::PORT_SUSPENDED, hub::PORT_OVER_CURRENT}) {
        Fixture f(4U); f.before[2U].status = bad;
        assert(f.run() == hub::PowerResult::Unavailable);
        assert(f.calls == 4U && f.waits == 0U); // No shared write.
    }
    { Fixture f(4U); f.before[3U].changed = hub::CHANGE_OVER_CURRENT;
      assert(f.run() == hub::PowerResult::Unavailable && f.calls == 4U && f.waits == 0U); }
    { Fixture f(4U); f.timer_ok = false;
      assert(f.run() == hub::PowerResult::UnsafePort && f.calls == 8U && f.waits == 1U); }
    for (uint16_t bad : {uint16_t{0U}, hub::PORT_ENABLED, hub::PORT_RESET,
                         hub::PORT_SUSPENDED, hub::PORT_OVER_CURRENT}) {
        Fixture f(4U); f.after[0U].status = hub::PORT_POWERED | bad;
        if (bad == 0U) f.after[0U].status = 0U;
        assert(f.run() == hub::PowerResult::UnsafePort && f.calls == 9U);
    }
    { Fixture f(4U); f.after[0U].changed = hub::CHANGE_OVER_CURRENT;
      assert(f.run() == hub::PowerResult::UnsafePort && f.calls == 9U); }
    { Fixture f(4U); for (auto& port : f.before) port.status = hub::PORT_POWERED | hub::PORT_ENABLED;
      assert(f.run() == hub::PowerResult::Ready && f.calls == 4U && f.waits == 0U); }
    for (auto mode : {hub::PowerMode::Individual, hub::PowerMode::AlwaysOn}) {
        Fixture f(4U); f.descriptor.power_mode = mode;
        assert(f.run() == hub::PowerResult::Unavailable && f.calls == 0U); }
    for (uint8_t count : {uint8_t{0U}, uint8_t{16U}}) {
        Fixture f(count); assert(f.run() == hub::PowerResult::Unavailable && f.calls == 0U); }
    std::puts("USB2 ganged power bounded group preflight, readback and fault containment: PASS");
}
