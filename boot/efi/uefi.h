#pragma once

#include <stddef.h>
#include <stdint.h>

#define EFIAPI __attribute__((ms_abi))

typedef void VOID;
typedef uint8_t BOOLEAN;
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef size_t UINTN;
typedef int64_t INT64;
typedef uint16_t CHAR16;
typedef UINT64 EFI_STATUS;
typedef VOID* EFI_HANDLE;
typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef UINT64 EFI_VIRTUAL_ADDRESS;

#define TRUE ((BOOLEAN)1)
#define FALSE ((BOOLEAN)0)
#define EFI_ERROR_BIT UINT64_C(0x8000000000000000)
#define EFIERR(code) (EFI_ERROR_BIT | (code))
#define EFI_ERROR(status) (((status) & EFI_ERROR_BIT) != 0)
#define EFI_SUCCESS UINT64_C(0)
#define EFI_LOAD_ERROR EFIERR(1)
#define EFI_INVALID_PARAMETER EFIERR(2)
#define EFI_UNSUPPORTED EFIERR(3)
#define EFI_BAD_BUFFER_SIZE EFIERR(4)
#define EFI_BUFFER_TOO_SMALL EFIERR(5)
#define EFI_NOT_READY EFIERR(6)
#define EFI_OUT_OF_RESOURCES EFIERR(9)
#define EFI_NOT_FOUND EFIERR(14)

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8 Data4[8];
} EFI_GUID;

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef enum {
    AllocateAnyPages = 0,
    AllocateMaxAddress = 1,
    AllocateAddress = 2,
    MaxAllocateType = 3
} EFI_ALLOCATE_TYPE;

typedef enum {
    EfiReservedMemoryType = 0,
    EfiLoaderCode = 1,
    EfiLoaderData = 2,
    EfiBootServicesCode = 3,
    EfiBootServicesData = 4,
    EfiRuntimeServicesCode = 5,
    EfiRuntimeServicesData = 6,
    EfiConventionalMemory = 7,
    EfiUnusableMemory = 8,
    EfiACPIReclaimMemory = 9,
    EfiACPIMemoryNVS = 10,
    EfiMemoryMappedIO = 11,
    EfiMemoryMappedIOPortSpace = 12,
    EfiPalCode = 13,
    EfiPersistentMemory = 14,
    EfiMaxMemoryType = 15
} EFI_MEMORY_TYPE;

typedef struct {
    UINT32 Type;
    UINT32 Padding;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
    struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* self,
    const CHAR16* string);

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    VOID* Reset;
    EFI_TEXT_STRING OutputString;
    VOID* TestString;
    VOID* QueryMode;
    VOID* SetMode;
    VOID* SetAttribute;
    VOID* ClearScreen;
    VOID* SetCursorPosition;
    VOID* EnableCursor;
    VOID* Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct {
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY)(
    struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL* self,
    EFI_INPUT_KEY* key);
typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    VOID* Reset;
    EFI_INPUT_READ_KEY ReadKeyStroke;
    VOID* WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

struct EFI_FILE_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(
    struct EFI_FILE_PROTOCOL* self,
    struct EFI_FILE_PROTOCOL** new_handle,
    const CHAR16* file_name,
    UINT64 open_mode,
    UINT64 attributes);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(
    struct EFI_FILE_PROTOCOL* self);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(
    struct EFI_FILE_PROTOCOL* self,
    UINTN* buffer_size,
    VOID* buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_POSITION)(
    struct EFI_FILE_PROTOCOL* self,
    UINT64* position);
typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_POSITION)(
    struct EFI_FILE_PROTOCOL* self,
    UINT64 position);

typedef struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_FILE_OPEN Open;
    EFI_FILE_CLOSE Close;
    VOID* Delete;
    EFI_FILE_READ Read;
    VOID* Write;
    EFI_FILE_GET_POSITION GetPosition;
    EFI_FILE_SET_POSITION SetPosition;
    VOID* GetInfo;
    VOID* SetInfo;
    VOID* Flush;
    VOID* OpenEx;
    VOID* ReadEx;
    VOID* WriteEx;
    VOID* FlushEx;
} EFI_FILE_PROTOCOL;

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_OPEN_VOLUME)(
    struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* self,
    EFI_FILE_PROTOCOL** root);
typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_OPEN_VOLUME OpenVolume;
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

struct EFI_SYSTEM_TABLE;
typedef struct {
    UINT32 Revision;
    UINT32 Padding;
    EFI_HANDLE ParentHandle;
    struct EFI_SYSTEM_TABLE* SystemTable;
    EFI_HANDLE DeviceHandle;
    VOID* FilePath;
    VOID* Reserved;
    UINT32 LoadOptionsSize;
    UINT32 Padding2;
    VOID* LoadOptions;
    VOID* ImageBase;
    UINT64 ImageSize;
    UINT32 ImageCodeType;
    UINT32 ImageDataType;
    VOID* Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor = 0,
    PixelBlueGreenRedReserved8BitPerColor = 1,
    PixelBitMask = 2,
    PixelBltOnly = 3,
    PixelFormatMax = 4
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    VOID* QueryMode;
    VOID* SetMode;
    VOID* Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef struct {
    EFI_GUID VendorGuid;
    VOID* VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(
    EFI_ALLOCATE_TYPE type,
    EFI_MEMORY_TYPE memory_type,
    UINTN pages,
    EFI_PHYSICAL_ADDRESS* memory);
typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(
    EFI_PHYSICAL_ADDRESS memory,
    UINTN pages);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
    UINTN* memory_map_size,
    EFI_MEMORY_DESCRIPTOR* memory_map,
    UINTN* map_key,
    UINTN* descriptor_size,
    UINT32* descriptor_version);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(
    EFI_MEMORY_TYPE pool_type,
    UINTN size,
    VOID** buffer);
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(VOID* buffer);
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(
    EFI_HANDLE handle,
    const EFI_GUID* protocol,
    VOID** interface);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(
    const EFI_GUID* protocol,
    VOID* registration,
    VOID** interface);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(
    EFI_HANDLE image_handle,
    UINTN map_key);
typedef EFI_STATUS (EFIAPI *EFI_STALL)(UINTN microseconds);

typedef struct {
    EFI_TABLE_HEADER Hdr;
    VOID* RaiseTPL;
    VOID* RestoreTPL;
    EFI_ALLOCATE_PAGES AllocatePages;
    EFI_FREE_PAGES FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;
    VOID* CreateEvent;
    VOID* SetTimer;
    VOID* WaitForEvent;
    VOID* SignalEvent;
    VOID* CloseEvent;
    VOID* CheckEvent;
    VOID* InstallProtocolInterface;
    VOID* ReinstallProtocolInterface;
    VOID* UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL HandleProtocol;
    VOID* Reserved;
    VOID* RegisterProtocolNotify;
    VOID* LocateHandle;
    VOID* LocateDevicePath;
    VOID* InstallConfigurationTable;
    VOID* LoadImage;
    VOID* StartImage;
    VOID* Exit;
    VOID* UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    VOID* GetNextMonotonicCount;
    EFI_STALL Stall;
    VOID* SetWatchdogTimer;
    VOID* ConnectController;
    VOID* DisconnectController;
    VOID* OpenProtocol;
    VOID* CloseProtocol;
    VOID* OpenProtocolInformation;
    VOID* ProtocolsPerHandle;
    VOID* LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
    VOID* InstallMultipleProtocolInterfaces;
    VOID* UninstallMultipleProtocolInterfaces;
    VOID* CalculateCrc32;
    VOID* CopyMem;
    VOID* SetMem;
    VOID* CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;
    CHAR16* FirmwareVendor;
    UINT32 FirmwareRevision;
    UINT32 Padding;
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL* ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* StdErr;
    VOID* RuntimeServices;
    EFI_BOOT_SERVICES* BootServices;
    UINTN NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE* ConfigurationTable;
} EFI_SYSTEM_TABLE;

#define EFI_FILE_MODE_READ UINT64_C(1)

static const EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID = {
    0x5B1B31A1, 0x9562, 0x11D2,
    {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}
};
static const EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = {
    0x964E5B22, 0x6459, 0x11D2,
    {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}
};
static const EFI_GUID EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID = {
    0x9042A9DE, 0x23DC, 0x4A38,
    {0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A}
};
static const EFI_GUID EFI_ACPI_20_TABLE_GUID = {
    0x8868E871, 0xE4F1, 0x11D3,
    {0xBC, 0x22, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81}
};
static const EFI_GUID EFI_ACPI_TABLE_GUID = {
    0xEB9D2D30, 0x2D88, 0x11D3,
    {0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D}
};

_Static_assert(sizeof(EFI_TABLE_HEADER) == 24, "UEFI table header ABI");
_Static_assert(offsetof(EFI_BOOT_SERVICES, AllocatePages) == 40,
               "UEFI AllocatePages offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, HandleProtocol) == 152,
               "UEFI HandleProtocol offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, ExitBootServices) == 232,
               "UEFI ExitBootServices offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, LocateProtocol) == 320,
               "UEFI LocateProtocol offset");
_Static_assert(offsetof(EFI_SYSTEM_TABLE, BootServices) == 96,
               "UEFI system-table ABI");
_Static_assert(sizeof(EFI_MEMORY_DESCRIPTOR) == 40,
               "UEFI memory descriptor ABI");
