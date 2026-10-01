#ifndef OS_PAGING_PLAN_H
#define OS_PAGING_PLAN_H
#include "storage/kernel_image.h"
#include "handoff/stack.h"

#define BOOT_MAPPING_LIMIT (ELF_MAX_LOAD_SEGMENTS + 2)
#define BOOT_INFO_VIRTUAL X86_BOOT_INFO_VIRTUAL
#define BOOT_STACK_GUARD X86_BOOT_STACK_GUARD
#define BOOT_STACK_VIRTUAL X86_BOOT_STACK_VIRTUAL

// ====== descrizione delle mappature senza proprieta delle pagine ====== //

typedef struct {
    UINT64 PhysicalBase;
    UINT64 VirtualBase;
    UINT64 Pages;
    UINT32 Flags;
} BOOT_MAPPING;

typedef struct {
    UINT64 Entry;
    UINT64 StackTop;
    UINT32 Count;
    BOOT_MAPPING Mappings[BOOT_MAPPING_LIMIT];
} BOOT_MAPPING_PLAN;

EFI_STATUS boot_validate_mapping_plan(const BOOT_MAPPING_PLAN *plan);
EFI_STATUS boot_plan_mappings(const BOOT_KERNEL_IMAGE *kernel,
                             const BOOT_KERNEL_STACK *stack, BOOT_MAPPING_PLAN *plan);
#endif
