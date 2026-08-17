#include "physical.hpp"

#include "e1000.hpp"
#include "pcnet.hpp"

namespace net::physical {
namespace {

Driver g_driver = Driver::None;
Status g_status = Status::NotFound;
bool g_initialized = false;
bool g_detected = false;

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

    g_initialized = true;
    g_status = g_detected ? Status::DeviceUnavailable : Status::NotFound;
    return g_status;
}

bool ready() {
    switch (g_driver) {
        case Driver::E1000: return e1000::interface() != nullptr;
        case Driver::Pcnet: return pcnet::interface() != nullptr;
        case Driver::None: return false;
    }
    return false;
}

bool detected() { return g_detected; }
Driver driver() { return ready() ? g_driver : Driver::None; }

NetworkInterface* interface() {
    switch (driver()) {
        case Driver::E1000: return e1000::interface();
        case Driver::Pcnet: return pcnet::interface();
        case Driver::None: return nullptr;
    }
    return nullptr;
}

const char* name() {
    switch (driver()) {
        case Driver::E1000: return "e1000";
        case Driver::Pcnet: return "pcnet";
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
    }
    return "unknown physical network status";
}

} // namespace net::physical
