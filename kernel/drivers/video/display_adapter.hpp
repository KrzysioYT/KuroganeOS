#pragma once

#include <stdint.h>

namespace drivers::video::display_adapter {

struct Info {
    bool present;
    bool gop_scanout;
    bool software_compositor;
    bool native_2d;
    bool accelerated_3d;
    bool virtio_gpu;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t subclass;
    uint32_t scanout_width;
    uint32_t scanout_height;
    uint32_t scanout_count;
};

// Discovers the active PCI display device. VirtIO-GPU is preferred when its
// modern control transport can be initialized because it gives KuroganeOS a
// native command path independent from the firmware GOP framebuffer. GOP and
// the software compositor remain the scanout fallback until native 2D resource
// ownership is enabled; accelerated_3d stays false until a real 3D backend is
// implemented and qualified.
void probe();
const Info& info();

} // namespace drivers::video::display_adapter
