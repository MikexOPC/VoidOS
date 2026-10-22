#ifndef X86_PAGING_GPU_VALIDATION_H
#define X86_PAGING_GPU_VALIDATION_H
#include "../inventory.h"
int x86_paging_gpu_inputs(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_PAGING_INVENTORY *output);
BOOT_U32 x86_paging_gpu_source(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 physical);
BOOT_U32 x86_paging_gpu_leaf(BOOT_U64 entry, BOOT_U64 address);
int x86_paging_gpu_record(const X86_PAGING_INVENTORY *snapshot, const X86_PAGING_RESOURCE *record);
int x86_paging_gpu_complete(const X86_PAGING_INVENTORY *snapshot);
#endif
