#ifndef OS_ELF_VALIDATE_H
#define OS_ELF_VALIDATE_H
#include "uefi.h"

// ====== limiti del profilo iniziale ====== //

#define ELF_MAX_PROGRAM_HEADERS 32
#define ELF_MAX_LOAD_SEGMENTS 16
#define ELF_MAX_FILE_BYTES (64ULL * 1024 * 1024)
#define ELF_MAX_MEMORY_BYTES (128ULL * 1024 * 1024)

typedef enum {
    ELF_VALID = 0,
    ELF_BAD_ARGUMENT,
    ELF_BAD_HEADER,
    ELF_UNSUPPORTED_PROFILE,
    ELF_BAD_TABLE,
    ELF_BAD_SEGMENT,
    ELF_OVERLAPPING_SEGMENTS,
    ELF_BAD_ENTRY,
    ELF_LIMIT_EXCEEDED
} ELF_VALIDATION_RESULT;

// ====== piano di caricamento senza allocazioni ====== //

typedef struct {
    UINT64 FileOffset;
    UINT64 FileBytes;
    UINT64 VirtualAddress;
    UINT64 MemoryBytes;
    UINT32 Flags;
} ELF_LOAD_SEGMENT;

typedef struct {
    UINT64 Entry;
    UINT64 TotalMemoryBytes;
    UINT32 SegmentCount;
    ELF_LOAD_SEGMENT Segments[ELF_MAX_LOAD_SEGMENTS];
} ELF_LOAD_PLAN;
typedef char check_load_plan_size[(sizeof(ELF_LOAD_PLAN) == 664) ? 1 : -1];

// ====== validazione buffer immutabile ====== //

ELF_VALIDATION_RESULT elf_validate(const UINT8 *file, UINTN size, ELF_LOAD_PLAN *plan);
#endif
