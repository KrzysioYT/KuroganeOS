#include "display_adapter.hpp"

#include "../framebuffer.hpp"
#include "../pci.hpp"

namespace drivers::video::display_adapter {
namespace {
Info g_info{};
bool g_probed = false;
}

void probe() {
    g_info = {};
    g_info.gop_scanout = graphics::available();
    g_info.software_compositor = graphics::available();
    g_info.accelerated_3d = false;

    for (size_t index = 0U; index < pci::device_count(); ++index) {
        const pci::Device* device = pci::device_at(index);
        if (device == nullptr || device->class_code != UINT8_C(0x03)) continue;
        g_info.present = true;
        g_info.vendor_id = device->vendor_id;
        g_info.device_id = device->device_id;
        g_info.subclass = device->subclass;
        break;
    }
    g_probed = true;
}

const Info& info() {
    if (!g_probed) probe();
    return g_info;
}

} // namespace drivers::video::display_adapter
