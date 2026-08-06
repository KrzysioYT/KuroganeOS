#pragma once

#include <stddef.h>
#include <stdint.h>

namespace pci {

struct Address {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
};

struct Device {
    Address address;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    uint8_t revision;
    uint8_t header_type;
    uint8_t interrupt_line;
};

using VisitCallback = bool (*)(const Device& device, void* context);

uint32_t read32(Address address, uint8_t offset);
uint16_t read16(Address address, uint8_t offset);
uint8_t read8(Address address, uint8_t offset);
void write32(Address address, uint8_t offset, uint32_t value);
void write16(Address address, uint8_t offset, uint16_t value);

void scan();
size_t device_count();
const Device* device_at(size_t index);
const Device* find(uint16_t vendor_id, uint16_t device_id,
                   size_t occurrence = 0);
const Device* find_class(uint8_t class_code, uint8_t subclass,
                         size_t occurrence = 0);
void visit(VisitCallback callback, void* context);
uint64_t bar_address(const Device& device, uint8_t bar_index,
                     bool* is_io = nullptr);
void enable_bus_mastering(const Device& device);

} // namespace pci
