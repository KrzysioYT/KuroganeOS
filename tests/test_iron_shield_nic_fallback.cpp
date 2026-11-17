#include "../kernel/net/physical.cpp"

#include <cassert>
#include <cstring>

/* Exercise the production physical NIC selection, not just PCI ID matching.
 * This host fixture supplies only PCI enumeration and backend interfaces:
 * it does not simulate a working RTL8168 DMA/PHY driver. */
namespace {
bool realtek_present = false;
net::virtio_net::Status virtio_result = net::virtio_net::Status::NoDevice;
net::e1000::Status intel_result = net::e1000::Status::NotFound;
net::pcnet::Status pcnet_result = net::pcnet::Status::NotFound;
net::e1000::Model intel_model = net::e1000::Model::I82574L;
net::rtl81xx::Status rtl_result = net::rtl81xx::Status::LinkDown;
net::NetworkInterface test_interface{};
const pci::Device realtek_device{
    {0U, 1U, 0U}, UINT16_C(0x10EC), UINT16_C(0x8168),
    2U, 0U, 0U, 0U, 0U, 0U
};
}

namespace pci {
const Device* find(uint16_t vendor, uint16_t device, size_t occurrence) {
    return realtek_present && occurrence == 0U &&
        vendor == realtek_device.vendor_id &&
        device == realtek_device.device_id ? &realtek_device : nullptr;
}
}

namespace net::rtl81xx {
Status initialize() { return realtek_present ? rtl_result : Status::NotFound; }
NetworkInterface* interface() {
    return realtek_present && rtl_result == Status::Ok ? &test_interface : nullptr;
}
bool link_up() { return realtek_present && rtl_result == Status::Ok; }
}

namespace net::virtio_net {
Status initialize() { return virtio_result; }
NetworkInterface* interface() {
    return virtio_result == Status::Ok ? &test_interface : nullptr;
}
}

namespace net::e1000 {
Status initialize() { return intel_result; }
NetworkInterface* interface() {
    return intel_result == Status::Ok ? &test_interface : nullptr;
}
Model model() { return intel_model; }
bool link_up() { return intel_result == Status::Ok; }
}

namespace net::pcnet {
Status initialize() { return pcnet_result; }
NetworkInterface* interface() {
    return pcnet_result == Status::Ok ? &test_interface : nullptr;
}
}

int main() {
    using net::physical::Status;
    /* Unsupported PCI hardware is present but cannot become an operational
     * NetworkInterface or be mistaken for an empty bus. */
    realtek_present = true;
    assert(net::physical::initialize() == Status::UnsupportedRealtek);
    assert(net::physical::detected());
    assert(!net::physical::ready());
    assert(net::physical::interface() == nullptr);
    assert(net::physical::driver() == net::physical::Driver::None);
    assert(net::physical::last_status() == Status::UnsupportedRealtek);

    /* When a real driver binds, the PCI model becomes usable via the
     * production interface, not merely through a classifier. */
    rtl_result = net::rtl81xx::Status::Ok;
    assert(net::physical::initialize() == Status::Ok);
    assert(net::physical::driver() == net::physical::Driver::Realtek);
    assert(net::physical::interface() == &test_interface);
    assert(std::strcmp(net::physical::name(), "rtl8168") == 0);
    rtl_result = net::rtl81xx::Status::LinkDown;
    assert(std::strcmp(
        net::physical::status_message(Status::UnsupportedRealtek),
        "Realtek RTL8111/8168 detected; driver not operational") == 0);

    /* No supported or unsupported NIC should be reported as NotFound. */
    realtek_present = false;
    assert(net::physical::initialize() == Status::NotFound);
    assert(!net::physical::detected());

    /* A recognized Intel NIC whose production probe fails is unavailable,
     * regardless of any other unsupported Realtek device on the bus. */
    realtek_present = true;
    intel_result = net::e1000::Status::DeviceError;
    assert(net::physical::initialize() == Status::DeviceUnavailable);
    assert(net::physical::detected());
    assert(!net::physical::ready());

    /* Once a driver is really bound, prefer it over the unsupported device. */
    intel_result = net::e1000::Status::Ok;
    assert(net::physical::initialize() == Status::Ok);
    assert(net::physical::driver() == net::physical::Driver::E1000);
    assert(net::physical::interface() == &test_interface);
    assert(net::physical::link_up());
    assert(std::strcmp(net::physical::name(), "e1000e") == 0);
    return 0;
}
