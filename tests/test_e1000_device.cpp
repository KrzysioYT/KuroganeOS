#include "../kernel/net/e1000_device.hpp"

#include <cstring>

int main() {
    using net::e1000::device::Model;

    if (net::e1000::device::identify(0x1234U, 0x100EU) != Model::Unsupported) {
        return 1;
    }
    if (net::e1000::device::identify(
            net::e1000::device::kIntelVendor,
            net::e1000::device::k82540Em) != Model::Intel82540Em) {
        return 2;
    }
    if (net::e1000::device::identify(
            net::e1000::device::kIntelVendor,
            net::e1000::device::k82574L) != Model::Intel82574L) {
        return 3;
    }
    if (net::e1000::device::supported(
            net::e1000::device::kIntelVendor, 0xFFFFU)) {
        return 4;
    }
    if (net::e1000::device::is_e1000e(Model::Intel82540Em) ||
        !net::e1000::device::is_e1000e(Model::Intel82574L)) {
        return 5;
    }
    if (std::strcmp(
            net::e1000::device::driver_name(Model::Intel82540Em),
            "e1000") != 0 ||
        std::strcmp(
            net::e1000::device::driver_name(Model::Intel82574L),
            "e1000e") != 0) {
        return 6;
    }
    if (std::strcmp(
            net::e1000::device::model_name(Model::Intel82574L),
            "Intel 82574L") != 0) {
        return 7;
    }
    return 0;
}
