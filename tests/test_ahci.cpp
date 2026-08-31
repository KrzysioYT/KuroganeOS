#include <stddef.h>
#include <stdint.h>

#include <cstdio>

#include "../kernel/memory/physical_memory.hpp"
#include "../kernel/storage/ahci_protocol.hpp"
#include "../kernel/storage/dma.hpp"

namespace {

using ProtocolStatus = storage::ahci::protocol::Status;
using LinkState = storage::ahci::protocol::LinkState;
using DmaStatus = storage::dma::Status;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            std::fprintf(                                                     \
                stderr,                                                       \
                "CHECK failed at %s:%d: %s\n",                              \
                __FILE__,                                                     \
                __LINE__,                                                     \
                #expression);                                                 \
            return false;                                                     \
        }                                                                     \
    } while (false)

void set_capacity(uint16_t* words, uint64_t sector_count) {
    words[100] = static_cast<uint16_t>(sector_count);
    words[101] = static_cast<uint16_t>(sector_count >> 16U);
    words[102] = static_cast<uint16_t>(sector_count >> 32U);
    words[103] = static_cast<uint16_t>(sector_count >> 48U);
}

void set_model(uint16_t* words, const char* model) {
    for (size_t index = 0U;
         index < storage::ahci::protocol::ATA_MODEL_CAPACITY / 2U;
         ++index) {
        const size_t first = index * 2U;
        const char high = model[first] != '\0' ? model[first] : ' ';
        const char low = model[first] != '\0' && model[first + 1U] != '\0'
            ? model[first + 1U]
            : ' ';
        words[27U + index] = static_cast<uint16_t>(
            (static_cast<uint16_t>(static_cast<unsigned char>(high)) << 8U) |
            static_cast<uint16_t>(static_cast<unsigned char>(low)));
        if (model[first] == '\0') {
            for (size_t rest = index + 1U;
                 rest < storage::ahci::protocol::ATA_MODEL_CAPACITY / 2U;
                 ++rest) {
                words[27U + rest] = UINT16_C(0x2020);
            }
            break;
        }
    }
}

void make_identify(uint16_t* words) {
    for (size_t index = 0U;
         index < storage::ahci::protocol::ATA_IDENTIFY_WORD_COUNT;
         ++index) {
        words[index] = 0U;
    }
    words[83] = UINT16_C(0x4400); // Valid command-set words and LBA48.
    set_capacity(words, UINT64_C(1048576));
    set_model(words, "Kurogane test disk");
}

bool test_bar_decode() {
    storage::ahci::protocol::PciBar bar{};
    CHECK(storage::ahci::protocol::decode_bar5(
              UINT32_C(0xFEBF0000), UINT32_C(0xFFFFE000), &bar) ==
          ProtocolStatus::Ok);
    CHECK(bar.physical_address == UINT64_C(0xFEBF0000));
    CHECK(bar.size == UINT32_C(8192));
    CHECK(!bar.prefetchable);

    CHECK(storage::ahci::protocol::decode_bar5(
              UINT32_C(0xFEBF0001), UINT32_C(0xFFFFE001), &bar) ==
          ProtocolStatus::UnsupportedPciBar);
    CHECK(storage::ahci::protocol::decode_bar5(
              UINT32_C(0xFEBF0004), UINT32_C(0xFFFFE004), &bar) ==
          ProtocolStatus::UnsupportedPciBar);
    CHECK(storage::ahci::protocol::decode_bar5(
              UINT32_C(0xFEBF0000), UINT32_C(0xFFFFFF00), &bar) ==
          ProtocolStatus::InvalidPciBarSize);
    CHECK(storage::ahci::protocol::decode_bar5(
              UINT32_C(0xFEBF1000), UINT32_C(0xFFFFE000), &bar) ==
          ProtocolStatus::InvalidPciBarSize);
    CHECK(storage::ahci::protocol::decode_bar5(
              UINT32_C(0xFEBF0008), UINT32_C(0xFFFFE008), &bar) ==
          ProtocolStatus::UnsupportedPciBar);
    return true;
}

bool test_sata_link_state_classification() {
    CHECK(storage::ahci::protocol::classify_sata_status(
              UINT32_C(0x000)) == LinkState::NoDevice);
    CHECK(storage::ahci::protocol::classify_sata_status(
              UINT32_C(0x001)) == LinkState::Transitional);
    CHECK(storage::ahci::protocol::classify_sata_status(
              UINT32_C(0x103)) == LinkState::Active);
    CHECK(storage::ahci::protocol::classify_sata_status(
              UINT32_C(0x003)) == LinkState::Transitional);
    CHECK(storage::ahci::protocol::classify_sata_status(
              UINT32_C(0x203)) == LinkState::Transitional);
    CHECK(storage::ahci::protocol::classify_sata_status(
              UINT32_C(0x004)) == LinkState::Offline);
    CHECK(storage::ahci::protocol::classify_sata_status(
              UINT32_C(0x002)) == LinkState::Unsupported);
    return true;
}

bool test_identify_512_byte_sectors() {
    uint16_t words[storage::ahci::protocol::ATA_IDENTIFY_WORD_COUNT]{};
    make_identify(words);
    storage::ahci::protocol::IdentifyInfo info{};
    CHECK(storage::ahci::protocol::parse_identify(words, &info) ==
          ProtocolStatus::Ok);
    CHECK(info.logical_sector_size == 512U);
    CHECK(info.sector_count == UINT64_C(1048576));
    CHECK(info.maximum_sectors_per_page == 8U);
    const char expected[] = "Kurogane test disk";
    for (size_t index = 0U; index < sizeof(expected); ++index) {
        CHECK(info.model[index] == expected[index]);
    }
    return true;
}

bool test_identify_4k_and_rejections() {
    uint16_t words[storage::ahci::protocol::ATA_IDENTIFY_WORD_COUNT]{};
    make_identify(words);
    words[106] = UINT16_C(0x5000);
    words[117] = 2048U;
    words[118] = 0U;
    storage::ahci::protocol::IdentifyInfo info{};
    CHECK(storage::ahci::protocol::parse_identify(words, &info) ==
          ProtocolStatus::Ok);
    CHECK(info.logical_sector_size == 4096U);
    CHECK(info.maximum_sectors_per_page == 1U);

    words[117] = 4096U;
    CHECK(storage::ahci::protocol::parse_identify(words, &info) ==
          ProtocolStatus::UnsupportedSectorSize);

    make_identify(words);
    words[83] = UINT16_C(0x0400);
    CHECK(storage::ahci::protocol::parse_identify(words, &info) ==
          ProtocolStatus::Lba48Unsupported);

    make_identify(words);
    set_capacity(words, 0U);
    CHECK(storage::ahci::protocol::parse_identify(words, &info) ==
          ProtocolStatus::InvalidCapacity);

    make_identify(words);
    set_capacity(words, UINT64_C(0x0002000000000000));
    CHECK(storage::ahci::protocol::parse_identify(words, &info) ==
          ProtocolStatus::InvalidCapacity);
    return true;
}

bool test_command_fis() {
    uint8_t fis[storage::ahci::protocol::REGISTER_HOST_TO_DEVICE_FIS_SIZE]{};
    constexpr uint64_t lba = UINT64_C(0x123456789ABC);
    CHECK(storage::ahci::protocol::build_register_fis(
              storage::ahci::protocol::AtaCommand::ReadDmaExt,
              lba,
              UINT16_C(0x1234),
              fis) == ProtocolStatus::Ok);
    CHECK(fis[0] == UINT8_C(0x27));
    CHECK(fis[1] == UINT8_C(0x80));
    CHECK(fis[2] == UINT8_C(0x25));
    CHECK(fis[4] == UINT8_C(0xBC));
    CHECK(fis[5] == UINT8_C(0x9A));
    CHECK(fis[6] == UINT8_C(0x78));
    CHECK(fis[7] == UINT8_C(0x40));
    CHECK(fis[8] == UINT8_C(0x56));
    CHECK(fis[9] == UINT8_C(0x34));
    CHECK(fis[10] == UINT8_C(0x12));
    CHECK(fis[12] == UINT8_C(0x34));
    CHECK(fis[13] == UINT8_C(0x12));

    CHECK(storage::ahci::protocol::build_register_fis(
              storage::ahci::protocol::AtaCommand::WriteDmaExt,
              1U,
              0U,
              fis) == ProtocolStatus::InvalidSectorCount);
    CHECK(storage::ahci::protocol::build_register_fis(
              storage::ahci::protocol::AtaCommand::WriteDmaExt,
              UINT64_C(0x102030),
              2U,
              fis) == ProtocolStatus::Ok);
    CHECK(fis[2] == UINT8_C(0x35));
    CHECK(fis[4] == UINT8_C(0x30));
    CHECK(fis[5] == UINT8_C(0x20));
    CHECK(fis[6] == UINT8_C(0x10));
    CHECK(fis[12] == 2U);
    CHECK(storage::ahci::protocol::build_register_fis(
              storage::ahci::protocol::AtaCommand::ReadDmaExt,
              storage::ahci::protocol::MAXIMUM_LBA48,
              2U,
              fis) == ProtocolStatus::InvalidLba);
    CHECK(storage::ahci::protocol::build_register_fis(
              storage::ahci::protocol::AtaCommand::IdentifyDevice,
              0U,
              0U,
              fis) == ProtocolStatus::Ok);
    CHECK(fis[2] == UINT8_C(0xEC));
    CHECK(storage::ahci::protocol::build_register_fis(
              storage::ahci::protocol::AtaCommand::FlushCacheExt,
              1U,
              0U,
              fis) == ProtocolStatus::InvalidArgument);
    CHECK(storage::ahci::protocol::build_register_fis(
              storage::ahci::protocol::AtaCommand::FlushCacheExt,
              0U,
              0U,
              fis) == ProtocolStatus::Ok);
    CHECK(fis[2] == UINT8_C(0xEA));
    return true;
}

bool test_dma_address_limit() {
    constexpr uintptr_t base = UINT64_C(0x00000000FFFFE000);
    constexpr size_t frame_size = 4096U;
    constexpr size_t frame_count = 4U;
    uint8_t bitmap[1]{};
    CHECK(memory::init_physical_memory_with_bitmap(
        base,
        frame_count * frame_size,
        frame_size,
        bitmap,
        sizeof(bitmap)));

    storage::dma::Page first{};
    storage::dma::Page second{};
    storage::dma::Page third{};
    CHECK(storage::dma::allocate_page(false, &first) == DmaStatus::Ok);
    CHECK(first.physical_address == UINT64_C(0x00000000FFFFE000));
    CHECK(storage::dma::allocate_page(false, &second) == DmaStatus::Ok);
    CHECK(second.physical_address == UINT64_C(0x00000000FFFFF000));
    CHECK(storage::dma::allocate_page(false, &third) ==
          DmaStatus::AddressNotSupported);
    CHECK(memory::used_frames() == 2U);

    CHECK(storage::dma::allocate_page(true, &third) == DmaStatus::Ok);
    CHECK(third.physical_address == UINT64_C(0x0000000100000000));
    CHECK(memory::used_frames() == 3U);

    storage::dma::Page corrupted = first;
    corrupted.physical_address += 4096U;
    CHECK(storage::dma::release_page(&corrupted) ==
          DmaStatus::InvalidArgument);
    CHECK(memory::used_frames() == 3U);
    CHECK(storage::dma::release_page(&first) == DmaStatus::Ok);
    CHECK(storage::dma::release_page(&second) == DmaStatus::Ok);
    CHECK(storage::dma::release_page(&third) == DmaStatus::Ok);
    CHECK(memory::used_frames() == 0U);
    return true;
}

} // namespace

int main() {
    struct TestCase {
        const char* name;
        bool (*run)();
    };
    const TestCase tests[] = {
        {"BAR5 decode", test_bar_decode},
        {"SATA link-state classification", test_sata_link_state_classification},
        {"IDENTIFY 512-byte sectors", test_identify_512_byte_sectors},
        {"IDENTIFY 4K and rejection paths", test_identify_4k_and_rejections},
        {"register H2D FIS", test_command_fis},
        {"DMA 32-bit address bound", test_dma_address_limit},
    };

    for (const TestCase& test : tests) {
        if (!test.run()) {
            std::fprintf(stderr, "[FAIL] %s\n", test.name);
            return 1;
        }
        std::printf("[PASS] %s\n", test.name);
    }
    return 0;
}
