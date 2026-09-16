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
    controller.maximum_ports = 0U;
    assert(first_connected_port(controller) == 0U);
    assert(!reset_connected_port(controller));

    std::puts("xHCI bounded port scan: PASS");
    return 0;
}
