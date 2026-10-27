#include "../kernel/storage/device_registry.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace {

storage::block::Status read_stub(
    void*, uint64_t, uint64_t, void*) {
    return storage::block::Status::Ok;
}
storage::block::Status write_stub(
    void*, uint64_t, uint64_t, const void*) {
    return storage::block::Status::Ok;
}
storage::block::Status flush_stub(void*) {
    return storage::block::Status::Ok;
}

} // namespace

int main() {
    using namespace storage;

    device_registry::reset();
    int context_a = 1;
    int context_b = 2;
    block::Device sata{
        &context_a, 512U, 4096U, read_stub, write_stub, flush_stub};
    block::Device nvme{
        &context_b, 4096U, 2048U, read_stub, write_stub, flush_stub};

    assert(device_registry::register_device(
        device_registry::Backend::Ahci, 0U, &sata, "SATA test"));
    assert(device_registry::register_device(
        device_registry::Backend::Nvme, 0U, &nvme, "NVMe test"));
    assert(device_registry::device_count() == 2U);

    device_registry::Entry entry{};
    assert(device_registry::entry_at(0U, &entry));
    assert(entry.device == &sata);
    assert(entry.backend == device_registry::Backend::Ahci);
    assert(device_registry::device_at(1U) == &nvme);

    // Re-registering the same device updates metadata but never duplicates it.
    assert(device_registry::register_device(
        device_registry::Backend::Other, 7U, &sata, "remapped SATA"));
    assert(device_registry::device_count() == 2U);
    assert(device_registry::entry_at(0U, &entry));
    assert(entry.backend == device_registry::Backend::Other);
    assert(entry.backend_index == 7U);

    assert(device_registry::unregister_device(&sata));
    assert(device_registry::device_count() == 1U);
    assert(device_registry::device_at(0U) == &nvme);
    assert(!device_registry::unregister_device(&sata));

    block::Device invalid{};
    assert(!device_registry::register_device(
        device_registry::Backend::Other, 0U, &invalid, "invalid"));
    assert(!device_registry::register_device(
        device_registry::Backend::Other, 0U, &nvme, ""));

    device_registry::reset();
    assert(device_registry::device_count() == 0U);
    std::cout << "Generic storage device registry: PASS\n";
    return 0;
}
