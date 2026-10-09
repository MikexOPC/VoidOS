#ifndef OS_BOOT_START_INFO_H
#define OS_BOOT_START_INFO_H
#include "lib/firmware/acpi/rsdp.h"
#include "kernel/drivers/video/framebuffer/framebuffer.h"
#include <stddef.h>

// ====== contratto numerico di ingresso x64 ====== //

typedef unsigned long long BOOT_U64;
typedef unsigned int BOOT_U32;
#define BOOT_START_SIGNATURE 0x314f464e49545342ULL
#define BOOT_START_VERSION 4U
#define BOOT_START_NXE 1ULL
#define BOOT_START_WP 2ULL
#define BOOT_START_FEATURES (BOOT_START_NXE | BOOT_START_WP)
#define BOOT_PROBE_SUCCESS 0x31454e4f444f534fULL

typedef struct {
    BOOT_U64 Signature;
    BOOT_U32 Version;
    BOOT_U32 Size;
    BOOT_U64 RequiredFeatures;
    BOOT_U64 EnabledFeatures;
    BOOT_U64 PageTableRoot;
    BOOT_U64 StackBase;
    BOOT_U64 StackBytes;
    BOOT_U64 KernelEntry;
    BOOT_U64 Reserved;
    BOOT_U64 MemoryMapVirtual;
    BOOT_U64 MemoryMapBytes;
    BOOT_U64 MemoryMapCapacity;
    BOOT_U64 MemoryDescriptorBytes;
    BOOT_U32 MemoryDescriptorVersion;
    BOOT_U32 MemoryReserved;
    OS_ACPI_ROOT Acpi;
    OS_FRAMEBUFFER Framebuffer;
} BOOT_START_INFO;

_Static_assert(offsetof(BOOT_START_INFO, Framebuffer) == 168, "Framebuffer handoff offset");
_Static_assert(sizeof(BOOT_START_INFO) == 208, "Contratto ingresso x64 versione 4");
#endif
