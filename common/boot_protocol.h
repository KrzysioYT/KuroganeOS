#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KUROGANE_BOOT_MAGIC UINT64_C(0x4B55524F47414E45)
#define KUROGANE_BOOT_PROTOCOL_VERSION 3u
#define KUROGANE_PAGE_SIZE UINT64_C(4096)
#define KUROGANE_MAX_MEMORY_REGIONS UINT64_C(4096)
#define KUROGANE_BOOT_FLAG_SAFE_MODE (UINT64_C(1) << 0)
#define KUROGANE_BOOT_FLAG_FORCE_DESKTOP (UINT64_C(1) << 1)
#define KUROGANE_BOOT_FLAG_DIAGNOSTICS (UINT64_C(1) << 2)
#define KUROGANE_BOOT_FLAG_INSTALLER (UINT64_C(1) << 3)
#define KUROGANE_BOOT_KNOWN_FLAGS \
    (KUROGANE_BOOT_FLAG_SAFE_MODE | \
     KUROGANE_BOOT_FLAG_FORCE_DESKTOP | \
     KUROGANE_BOOT_FLAG_DIAGNOSTICS | \
     KUROGANE_BOOT_FLAG_INSTALLER)

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define KUROGANE_SYSV_ABI __attribute__((sysv_abi))
#else
#error "Kurogane boot protocol requires an x86-64 GCC/Clang sysv_abi target"
#endif

typedef enum KuroganePixelFormat {
    KUROGANE_PIXEL_BGRX8 = 0,
    KUROGANE_PIXEL_RGBX8 = 1,
    KUROGANE_PIXEL_BITMASK = 2,
    KUROGANE_PIXEL_BLT_ONLY = 3
} KuroganePixelFormat;

typedef struct KuroganeFramebuffer {
    uint32_t* base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t pixel_format;
} KuroganeFramebuffer;

typedef enum KuroganeMemoryType {
    KUROGANE_MEMORY_RESERVED = 0,
    KUROGANE_MEMORY_USABLE = 1,
    KUROGANE_MEMORY_ACPI_RECLAIMABLE = 2,
    KUROGANE_MEMORY_ACPI_NVS = 3,
    KUROGANE_MEMORY_MMIO = 4,
    KUROGANE_MEMORY_BOOTLOADER_RECLAIMABLE = 5,
    KUROGANE_MEMORY_KERNEL = 6,
    KUROGANE_MEMORY_FRAMEBUFFER = 7
} KuroganeMemoryType;

typedef struct KuroganeMemoryRegion {
    uint64_t physical_start;
    uint64_t page_count;
    uint32_t type;
    uint32_t reserved;
    uint64_t attributes;
} KuroganeMemoryRegion;

typedef struct KuroganeBootInfo {
    uint64_t magic;
    uint32_t version;
    uint32_t size;
    KuroganeFramebuffer framebuffer;
    const KuroganeMemoryRegion* memory_regions;
    uint64_t memory_region_count;
    uint64_t rsdp_address;
    uint64_t kernel_physical_start;
    uint64_t kernel_physical_end;
    uint64_t flags;
    const void* installation_package;
    uint64_t installation_package_size;
} KuroganeBootInfo;

/* Legacy BOOTX64.EFI ABI retained only for compatibility tooling. */
typedef struct KuroganeLegacyFramebuffer {
    uint32_t* base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
} KuroganeLegacyFramebuffer;

typedef void (KUROGANE_SYSV_ABI *KuroganeKernelEntry)(void* boot_argument);

#if defined(__cplusplus)
static_assert(sizeof(void*) == 8, "Kurogane boot protocol requires x86-64");
static_assert(sizeof(KuroganeFramebuffer) == 32, "framebuffer ABI mismatch");
static_assert(sizeof(KuroganeMemoryRegion) == 32, "memory-region ABI mismatch");
static_assert(sizeof(KuroganeBootInfo) == 112, "boot-info ABI mismatch");
static_assert(offsetof(KuroganeBootInfo, framebuffer) == 16,
              "boot-info framebuffer offset mismatch");
static_assert(offsetof(KuroganeBootInfo, memory_regions) == 48,
              "boot-info memory-map offset mismatch");
static_assert(offsetof(KuroganeBootInfo, memory_region_count) == 56,
              "boot-info memory-count offset mismatch");
static_assert(offsetof(KuroganeBootInfo, rsdp_address) == 64,
              "boot-info RSDP offset mismatch");
static_assert(offsetof(KuroganeBootInfo, kernel_physical_start) == 72,
              "boot-info kernel-start offset mismatch");
static_assert(offsetof(KuroganeBootInfo, kernel_physical_end) == 80,
              "boot-info kernel-end offset mismatch");
static_assert(offsetof(KuroganeBootInfo, flags) == 88,
              "boot-info flags offset mismatch");
static_assert(offsetof(KuroganeBootInfo, installation_package) == 96,
              "boot-info installer-package offset mismatch");
static_assert(offsetof(KuroganeBootInfo, installation_package_size) == 104,
              "boot-info installer-size offset mismatch");
#else
_Static_assert(sizeof(void*) == 8, "Kurogane boot protocol requires x86-64");
_Static_assert(sizeof(KuroganeFramebuffer) == 32, "framebuffer ABI mismatch");
_Static_assert(sizeof(KuroganeMemoryRegion) == 32, "memory-region ABI mismatch");
_Static_assert(sizeof(KuroganeBootInfo) == 112, "boot-info ABI mismatch");
_Static_assert(offsetof(KuroganeBootInfo, framebuffer) == 16,
               "boot-info framebuffer offset mismatch");
_Static_assert(offsetof(KuroganeBootInfo, memory_regions) == 48,
               "boot-info memory-map offset mismatch");
_Static_assert(offsetof(KuroganeBootInfo, memory_region_count) == 56,
               "boot-info memory-count offset mismatch");
_Static_assert(offsetof(KuroganeBootInfo, rsdp_address) == 64,
               "boot-info RSDP offset mismatch");
_Static_assert(offsetof(KuroganeBootInfo, kernel_physical_start) == 72,
               "boot-info kernel-start offset mismatch");
_Static_assert(offsetof(KuroganeBootInfo, kernel_physical_end) == 80,
               "boot-info kernel-end offset mismatch");
_Static_assert(offsetof(KuroganeBootInfo, flags) == 88,
               "boot-info flags offset mismatch");
_Static_assert(offsetof(KuroganeBootInfo, installation_package) == 96,
               "boot-info installer-package offset mismatch");
_Static_assert(offsetof(KuroganeBootInfo, installation_package_size) == 104,
               "boot-info installer-size offset mismatch");
#endif

#ifdef __cplusplus
}
#endif
