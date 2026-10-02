#ifndef OS_BOOT_MEMORY_MAP_H
#define OS_BOOT_MEMORY_MAP_H
#include "file_protocol.h"
#include "arch/x86/configs/boot_profile.h"

#define BOOT_MAP_MAX_BYTES X86_BOOT_MAP_MAX_BYTES
#define BOOT_MAP_MAX_DESCRIPTORS 4096ULL

// ====== proprietario del buffer e validita della fotografia ====== //

typedef struct {
    void *Buffer;
    UINTN Capacity;
    UINTN Size;
    UINTN Key;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;
    UINT32 Valid;
} BOOT_MEMORY_MAP;

EFI_STATUS boot_acquire_memory_map(EFI_BOOT_SERVICES_PREFIX *services, BOOT_MEMORY_MAP *map);
EFI_STATUS boot_release_memory_map(EFI_BOOT_SERVICES_PREFIX *services, BOOT_MEMORY_MAP *map);
EFI_STATUS boot_validate_memory_map(const BOOT_MEMORY_MAP *map);
#endif
