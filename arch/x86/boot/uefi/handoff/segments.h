#ifndef OS_HANDOFF_SEGMENTS_H
#define OS_HANDOFF_SEGMENTS_H
#include "elf/load.h"

#define BOOT_SEGMENTS_SIGNATURE 0x5345474d454e5453ULL
#define BOOT_SEGMENTS_VERSION 1U

// ====== descrizione numerica delle pagine del kernel ====== //

typedef struct {
    UINT64 PhysicalBase;
    UINT64 VirtualBase;
    UINT64 Bytes;
    UINT32 Flags;
    UINT32 Reserved;
} BOOT_SEGMENT;

typedef struct {
    UINT64 Signature;
    UINT32 Version;
    UINT32 Size;
    UINT64 VirtualEntry;
    UINT64 AllocatedBytes;
    UINT32 SegmentCount;
    UINT32 Reserved;
    BOOT_SEGMENT Segments[ELF_MAX_LOAD_SEGMENTS];
} BOOT_SEGMENTS;

typedef char check_boot_segment_size[(sizeof(BOOT_SEGMENT) == 32) ? 1 : -1];
typedef char check_boot_segments_size[(sizeof(BOOT_SEGMENTS) == 552) ? 1 : -1];

EFI_STATUS boot_describe_segments(const ELF_LOAD_PLAN *plan, const ELF_STAGED_IMAGE *image,
                                  BOOT_SEGMENTS *description);
#endif
