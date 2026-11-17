#include "physical.hpp"

#include "virtio_net.hpp"
#include "e1000.hpp"
#include "pcnet.hpp"
#include "rtl81xx_detection.hpp"
#include "rtl81xx.hpp"
#include "../drivers/pci.hpp"

namespace net::physical {
namespace {

Driver g_driver = Driver::None;
Status g_status = Status::NotFound;
bool g_initialized = false;
bool g_detected = false;

bool virtio_present(virtio_net::Status status) {
    return status != virtio_net::Status::NoDevice;
}

bool e1000_present(e1000::Status status) {
    return status != e1000::Status::NotFound;
}

bool pcnet_present(pcnet::Status status) {
    return status != pcnet::Status::NotFound;
}

} // namespace

Status initialize() {
    if (g_initialized && ready()) return Status::AlreadyInitialized;
    g_driver = Driver::None;
    g_status = Status::NotFound;
    g_detected = false;

    const virtio_net::Status virtio_status = virtio_net::initialize();
    if (virtio_present(virtio_status)) g_detected = true;
    if ((virtio_status == virtio_net::Status::Ok ||
         virtio_status == virtio_net::Status::AlreadyInitialized) &&
        virtio_net::interface() != nullptr) {
        g_driver = Driver::VirtioNet;
        g_status = Status::Ok;
        g_initialized = true;
        return g_status;
    }

    /* The real RTL8168 backend is attempted before legacy Intel/PCnet.
     * A failed or link-down Realtek must never block other supported NICs. */
    const rtl81xx::Status realtek_status = rtl81xx::initialize();
    if (realtek_status != rtl81xx::Status::NotFound) g_detected = true;
    if ((realtek_status == rtl81xx::Status::Ok ||
         realtek_status == rtl81xx::Status::AlreadyInitialized) &&
        rtl81xx::interface() != nullptr) {
        g_driver = Driver::Realtek;
        g_status = Status::Ok;
        g_initialized = true;
        return g_status;
    }

    const e1000::Status e1000_status = e1000::initialize();
    if (e1000_present(e1000_status)) g_detected = true;
    if ((e1000_status == e1000::Status::Ok ||
         e1000_status == e1000::Status::AlreadyInitialized) &&
        e1000::interface() != nullptr) {
        g_driver = Driver::E1000;
        g_status = Status::Ok;
        g_initialized = true;
        return g_status;
    }

    const pcnet::Status pcnet_status = pcnet::initialize();
    if (pcnet_present(pcnet_status)) g_detected = true;
    if ((pcnet_status == pcnet::Status::Ok ||
         pcnet_status == pcnet::Status::AlreadyInitialized) &&
        pcnet::interface() != nullptr) {
        g_driver = Driver::Pcnet;
        g_status = Status::Ok;
        g_initialized = true;
        return g_status;
    }

    /* Expose a recognized but non-operational Realtek to diagnostics; a
     * failed probe does not mean the PCI bus was empty. */
    const pci::Device* realtek = pci::find(UINT16_C(0x10EC), UINT16_C(0x8168));
    const bool unsupported_realtek = realtek != nullptr &&
        rtl81xx::classify_model(realtek->vendor_id, realtek->device_id) !=
            rtl81xx::Model::Unknown;
    /* PCI detection also covers a backend that failed before binding. */
    if (unsupported_realtek) g_detected = true;
    g_initialized = true;
    g_status = unsupported_realtek && !virtio_present(virtio_status) &&
            !e1000_present(e1000_status) && !pcnet_present(pcnet_status)
        ? Status::UnsupportedRealtek
        : g_detected ? Status::DeviceUnavailable : Status::NotFound;
    return g_status;
}

Status last_status() { return g_status; }

bool ready() {
    switch (g_driver) {
        case Driver::VirtioNet: return virtio_net::interface() != nullptr;
        case Driver::E1000: return e1000::interface() != nullptr;
        case Driver::Pcnet: return pcnet::interface() != nullptr;
        case Driver::Realtek: return rtl81xx::interface() != nullptr;
        case Driver::None: return false;
    }
    return false;
}

bool link_up() {
    if (!ready()) return false;
    switch (g_driver) {
        case Driver::E1000: return e1000::link_up();
        case Driver::Realtek: return rtl81xx::link_up();
        case Driver::VirtioNet:
        case Driver::Pcnet: return true;
        case Driver::None: return false;
    }
    return false;
}

bool detected() { return g_detected; }
Driver driver() { return ready() ? g_driver : Driver::None; }

NetworkInterface* interface() {
    switch (driver()) {
        case Driver::VirtioNet: return virtio_net::interface();
        case Driver::E1000: return e1000::interface();
        case Driver::Pcnet: return pcnet::interface();
        case Driver::Realtek: return rtl81xx::interface();
        case Driver::None: return nullptr;
    }
    return nullptr;
}

const char* name() {
    switch (driver()) {
        case Driver::VirtioNet: return "virtio-net";
        case Driver::E1000:
            return e1000::model() == e1000::Model::I82574L
                ? "e1000e"
                : "e1000";
        case Driver::Pcnet: return "pcnet";
        case Driver::Realtek: return "rtl8168";
        case Driver::None: return "none";
    }
    return "none";
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyInitialized: return "already initialized";
        case Status::NotFound: return "no supported physical NIC was found";
        case Status::DeviceUnavailable: return "supported physical NIC detected but unavailable";
        case Status::UnsupportedRealtek:
            return "Realtek RTL8111/8168 detected; driver not operational";
    }
    return "unknown physical network status";
}

} // namespace net::physical
