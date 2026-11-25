#!/usr/bin/env python3
"""Enable the isolated RTL8168 boot-time shutdown/rebind probe."""
import argparse
from pathlib import Path


PROBE = r'''
net::Status qualify_rtl8168_rebind(net::Status initial_status) {
    const auto* target = pci::find(0x10ECU, 0x8168U);
    if (target == nullptr) {
        terminal::println("[RTL8168-QUAL] fixture absent: SKIP");
        return initial_status;
    }

    const auto print_value = [](const char* label, uint64_t value) {
        terminal::write(label);
        terminal::write_hex(value);
        terminal::println();
    };
    print_value("[RTL8168-QUAL] PCI revision: ", target->revision);
    print_value("[RTL8168-QUAL] PCI subsystem: ", pci::read32(*target, 0x2CU));
    print_value("[RTL8168-QUAL] MAC XID: ", net::rtl81xx::mac_xid());
    const auto stop = [] {
        const auto status = net::rtl81xx::shutdown();
        if (status != net::rtl81xx::Status::Ok) {
            terminal::println(net::rtl81xx::status_message(status));
            boot_failure("RTL8168-QUAL", "shutdown unverified; power off fixture");
        }
    };
    const auto selected = [](net::Status status) {
        return status == net::Status::Ok && net::service::physical_interface() &&
            net::service::dhcp_configured() &&
            net::physical::driver() == net::physical::Driver::Realtek;
    };
    if (!selected(initial_status) ||
        net::service::ping_gateway(0x8101U) != net::Status::Ok) {
        terminal::println(net::rtl81xx::status_message(net::rtl81xx::last_status()));
        stop();
        boot_failure("RTL8168-QUAL", "RTL8168 DHCP or baseline gateway echo failed");
    }
    terminal::println("[RTL8168-QUAL] baseline DHCP and gateway echo: PASS");

    // Offer a labelled Ethernet frame immediately before shutdown. A peer
    // capture can distinguish it from DHCP/ICMP and check delivery.
    auto* interface = net::rtl81xx::interface();
    if (interface == nullptr) {
        stop();
        boot_failure("RTL8168-QUAL", "interface lost before shutdown trial");
    }
    const net::MacAddress original_mac = interface->hardware_address;
    uint8_t frame[60]{};
    uint64_t observed_mac = 0U;
    for (size_t i = 0U; i < net::MAC_ADDRESS_LENGTH; ++i) {
        frame[i] = UINT8_MAX;
        frame[net::MAC_ADDRESS_LENGTH + i] = original_mac.bytes[i];
        observed_mac = (observed_mac << 8U) | original_mac.bytes[i];
    }
    print_value("[RTL8168-QUAL] MAC: ", observed_mac);
    frame[12U] = 0x88U;
    frame[13U] = 0xB5U;
    constexpr char payload[] = "KuroganeOS RTL8168 pre-stop";
    for (size_t i = 0U; i < sizeof(payload) - 1U; ++i)
        frame[14U + i] = static_cast<uint8_t>(payload[i]);
    const auto queued = net::interface_transmit(interface, frame, sizeof(frame));
    stop();
    if (queued != net::Status::Ok) {
        boot_failure("RTL8168-QUAL", "no diagnostic frame queued before shutdown");
    }
    terminal::println("[RTL8168-QUAL] frame queued and DMA shutdown verified: PASS");

    const auto rebound = net::service::initialize();
    interface = net::rtl81xx::interface();
    if (!selected(rebound) || interface == nullptr ||
        !net::mac_equal(original_mac, interface->hardware_address) ||
        net::service::ping_gateway(0x8102U) != net::Status::Ok) {
        stop();
        boot_failure("RTL8168-QUAL", "RTL8168 rebind with a fresh DHCP lease failed");
    }
    terminal::println("[RTL8168-QUAL] rebind with same MAC, DHCP and gateway echo: PASS");
    return rebound;
}

'''


def insert_once(source: str, anchor: str, replacement: str) -> str:
    if source.count(anchor) != 1:
        raise ValueError(f"missing or repeated qualification anchor: {anchor!r}")
    return source.replace(anchor, replacement, 1)


def inject(root: Path) -> None:
    selector = root / "kernel/net/physical.cpp"
    entrypoint = root / "kernel/main.cpp"
    selector_source = selector.read_text(encoding="utf-8")
    main_source = entrypoint.read_text(encoding="utf-8")
    define = "#define KUROGANE_EXPERIMENTAL_RTL81XX 1"
    if define in selector_source or "qualify_rtl8168_rebind(" in main_source:
        raise ValueError("RTL8168 qualification is already injected")

    first_include = '#include "physical.hpp"'
    selector_source = insert_once(
        selector_source, first_include, f"{define}\n{first_include}"
    )
    network_include = '#include "net/physical.hpp"'
    main_source = insert_once(
        main_source, network_include, f'{network_include}\n#include "net/rtl81xx.hpp"'
    )
    stack_trace = "void print_stack_trace(uint64_t initial_frame_pointer) {"
    main_source = insert_once(main_source, stack_trace, PROBE + stack_trace)
    network_start = """    const auto network_status = context.safe_mode
        ? net::Status::NotInitialized
        : net::service::initialize();"""
    main_source = insert_once(
        main_source,
        network_start,
        """    auto network_status = context.safe_mode
        ? net::Status::NotInitialized
        : net::service::initialize();
    if (!context.safe_mode) network_status = qualify_rtl8168_rebind(network_status);""",
    )
    selector.write_text(selector_source, encoding="utf-8")
    entrypoint.write_text(main_source, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", type=Path, default=Path.cwd())
    inject(parser.parse_args().root)
