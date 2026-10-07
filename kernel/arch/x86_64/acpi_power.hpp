#pragma once

#include <stdint.h>

namespace arch::x86_64::acpi_power {

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    FadtNotFound,
    InvalidFadt,
    DsdtNotFound,
    InvalidDsdt,
    SleepStateNotFound,
    NoPowerMethod,
};

struct ResetRegister {
    uint8_t address_space_id;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t address;
};

struct Configuration {
    uint16_t pm1a_control_port;
    uint16_t pm1b_control_port;
    uint32_t smi_command_port;
    uint8_t acpi_enable_value;
    uint8_t sleep_type_a;
    uint8_t sleep_type_b;
    ResetRegister reset_register;
    uint8_t reset_value;
    bool shutdown_supported;
    bool reset_supported;
};

Status parse(const void* rsdp, Configuration* output);
Status initialize(uint64_t rsdp_physical_address);

bool initialized();
bool shutdown_available();
bool reset_available();
const Configuration* configuration();

uint16_t compose_sleep_control(uint16_t current, uint8_t sleep_type);

void request_poweroff();
void request_reset();

const char* status_message(Status status);

} // namespace arch::x86_64::acpi_power
