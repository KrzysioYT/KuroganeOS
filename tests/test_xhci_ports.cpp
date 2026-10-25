#include <cassert>
#include <cstdio>

// Execute the production scanner against a bounded MMIO fixture. Link-time
// section collection discards unrelated privileged initialization paths.
#include "../kernel/drivers/usb/xhci.cpp"

int main() {
    using namespace drivers::usb::xhci;
    alignas(4) uint8_t registers[OP_PORTS + 255U * PORT_STRIDE]{};
    Controller controller{};
    controller.operational = registers;
    controller.maximum_ports = 255U;

    assert(first_connected_port(controller) == 0U);
    assert(!reset_connected_port(controller));
    assert(controller.port_id == 0U);

    // Last legal port must be found without probing a wrapped port zero.
    write32(controller.operational, OP_PORTS + 254U * PORT_STRIDE,
            PORT_CONNECTED);
    assert(first_connected_port(controller) == 255U);
    controller.maximum_ports = 254U;
    assert(first_connected_port(controller) == 0U);

    write32(controller.operational, OP_PORTS, PORT_CONNECTED);
    assert(first_connected_port(controller) == 1U);

    // Multi-HID enumeration must skip a port already owned by the primary
    // slot and return the second connected device without wrapping/scanning
    // outside MaxPorts.
    controller.maximum_ports = 2U;
    write32(controller.operational, OP_PORTS + PORT_STRIDE, PORT_CONNECTED);
    controller.slot_id = 1U;
    controller.port_id = 1U;
    assert(first_unclaimed_connected_port(controller) == 2U);
    controller.companion.slot_id = 2U;
    controller.companion.port_id = 2U;
    assert(first_unclaimed_connected_port(controller) == 0U);
    controller.slot_id = 0U;
    controller.port_id = 0U;
    controller.companion.slot_id = 0U;
    controller.companion.port_id = 0U;

    controller.maximum_ports = 0U;
    assert(first_connected_port(controller) == 0U);
    assert(!reset_connected_port(controller));

    std::puts("xHCI bounded port scan: PASS");
    return 0;
}
