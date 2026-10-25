#include <assert.h>
#include <stdio.h>

#include "../kernel/arch/x86_64/io_apic_irq.hpp"

namespace {

void test_defaults() {
    arch::x86_64::io_apic::LegacyRoute route{};
    assert(arch::x86_64::io_apic::resolve_legacy_irq(0, nullptr, 0, &route) ==
           arch::x86_64::io_apic::LegacyStatus::Ok);
    assert(route.global_system_interrupt == 0U);
    assert(route.trigger == arch::x86_64::io_apic::TriggerMode::Edge);
    assert(route.polarity == arch::x86_64::io_apic::Polarity::ActiveHigh);
}

void test_override() {
    const arch::x86_64::acpi::InterruptOverride override{
        0U, 1U, 9U, 0x000FU};
    arch::x86_64::io_apic::LegacyRoute route{};
    assert(arch::x86_64::io_apic::resolve_legacy_irq(
               1U, &override, 1U, &route) ==
           arch::x86_64::io_apic::LegacyStatus::Ok);
    assert(route.global_system_interrupt == 9U);
    assert(route.trigger == arch::x86_64::io_apic::TriggerMode::Level);
    assert(route.polarity == arch::x86_64::io_apic::Polarity::ActiveLow);

    const arch::x86_64::acpi::InterruptOverride conforming{
        0U, 2U, 2U, 0x0000U};
    assert(arch::x86_64::io_apic::resolve_legacy_irq(
               2U, &conforming, 1U, &route) ==
           arch::x86_64::io_apic::LegacyStatus::Ok);
    assert(route.global_system_interrupt == 2U);
    assert(route.trigger == arch::x86_64::io_apic::TriggerMode::Edge);
    assert(route.polarity == arch::x86_64::io_apic::Polarity::ActiveHigh);
}

void test_rejects_malformed() {
    arch::x86_64::acpi::InterruptOverride invalid_flags{
        0U, 3U, 3U, 0x0008U};
    arch::x86_64::io_apic::LegacyRoute route{};
    assert(arch::x86_64::io_apic::resolve_legacy_irq(
               3U, &invalid_flags, 1U, &route) ==
           arch::x86_64::io_apic::LegacyStatus::InvalidOverride);

    invalid_flags.flags = 0x0010U;
    assert(arch::x86_64::io_apic::resolve_legacy_irq(
               3U, &invalid_flags, 1U, &route) ==
           arch::x86_64::io_apic::LegacyStatus::InvalidOverride);

    const arch::x86_64::acpi::InterruptOverride duplicate[] = {
        {0U, 4U, 4U, 0x0001U},
        {0U, 4U, 5U, 0x0001U},
    };
    assert(arch::x86_64::io_apic::resolve_legacy_irq(
               4U, duplicate, 2U, &route) ==
           arch::x86_64::io_apic::LegacyStatus::ConflictingOverride);

    assert(arch::x86_64::io_apic::resolve_legacy_irq(
               16U, nullptr, 0U, &route) ==
           arch::x86_64::io_apic::LegacyStatus::InvalidIrq);
    assert(arch::x86_64::io_apic::resolve_legacy_irq(
               0U, nullptr, 0U, nullptr) ==
           arch::x86_64::io_apic::LegacyStatus::InvalidArgument);
}

} // namespace

int main() {
    test_defaults();
    test_override();
    test_rejects_malformed();
    puts("[test_io_apic_irq] PASS");
    return 0;
}
