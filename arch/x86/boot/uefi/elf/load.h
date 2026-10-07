#ifndef OS_ELF_LOAD_H
#define OS_ELF_LOAD_H
#include "elf/validate.h"
#include "file_protocol.h"

// ====== segmenti preparati in pagine fisiche ======

// Intel SDM Vol. 3A, Section 4.5: 4-level canonical bits 63:47 identical.
// Keep identity staging in the lower canonical half, excluding page zero.
#define ELF_STAGING_ADDRESS_LIMIT (1ULL << 47)

UINT32 elf_check_staging_range(UINT64 address, UINT64 pages);

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
