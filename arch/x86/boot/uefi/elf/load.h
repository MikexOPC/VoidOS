#ifndef OS_ELF_LOAD_H
#define OS_ELF_LOAD_H
#include "elf/validate.h"
#include "file_protocol.h"

// ====== segmenti preparati in pagine fisiche ====== //

typedef struct {
    UINT64 PhysicalBase;
    UINT64 VirtualBase;
    UINTN Pages;
    UINT32 Flags;
} ELF_STAGED_SEGMENT;

typedef struct {
    UINT32 Count;
    UINT64 AllocatedBytes;
    ELF_STAGED_SEGMENT Segments[ELF_MAX_LOAD_SEGMENTS];
} ELF_STAGED_IMAGE;

EFI_STATUS elf_stage_segments(EFI_BOOT_SERVICES_PREFIX *services, const UINT8 *file,
                             UINTN size, ELF_STAGED_IMAGE *image);
EFI_STATUS elf_release_segments(EFI_BOOT_SERVICES_PREFIX *services, ELF_STAGED_IMAGE *image);
#endif
