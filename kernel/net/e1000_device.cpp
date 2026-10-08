#include "e1000_device.hpp"

namespace net::e1000::device {

Model identify(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id != kIntelVendor) return Model::Unsupported;
    switch (device_id) {
        case k82540Em: return Model::Intel82540Em;
        case k82574L: return Model::Intel82574L;
        default: return Model::Unsupported;
    }
}

bool supported(uint16_t vendor_id, uint16_t device_id) {
    return identify(vendor_id, device_id) != Model::Unsupported;
}

bool is_e1000e(Model model) {
    return model == Model::Intel82574L;
}

const char* driver_name(Model model) {
    switch (model) {
        case Model::Intel82540Em: return "e1000";
        case Model::Intel82574L: return "e1000e";
        case Model::Unsupported: return "none";
    }
    return "none";
}

const char* model_name(Model model) {
    switch (model) {
        case Model::Intel82540Em: return "Intel 82540EM";
        case Model::Intel82574L: return "Intel 82574L";
        case Model::Unsupported: return "unsupported Intel Ethernet";
    }
    return "unsupported Intel Ethernet";
}

} // namespace net::e1000::device
