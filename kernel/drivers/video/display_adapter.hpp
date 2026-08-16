#pragma once

#include <stdint.h>

namespace drivers::video::display_adapter {

struct Info {
    bool present;
    bool gop_scanout;
    bool software_compositor;
    bool accelerated_3d;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t subclass;
};

// Discovers the first PCI display-class device. 3.3.3 deliberately keeps
// accelerated_3d=false until a real command submission driver exists; UEFI GOP
// scanout plus the Red Flux software compositor is the active graphics backend.
void probe();
const Info& info();

} // namespace drivers::video::display_adapter
