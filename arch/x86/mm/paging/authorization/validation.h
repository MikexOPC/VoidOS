#ifndef OS_X86_PAGING_AUTHORIZATION_H
#define OS_X86_PAGING_AUTHORIZATION_H
#include "../inventory.h"

int x86_paging_buffers_valid(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_PAGING_INVENTORY *output);
BOOT_U32 x86_paging_table_source(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_PAGING_VIDEO_AUTH *video,
    BOOT_U64 physical);
int x86_paging_video_inputs(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_PAGING_VIDEO_AUTH *video,
    const X86_PAGING_INVENTORY *output);
int x86_paging_video_tables_complete(const X86_PAGING_VIDEO_AUTH *video,
    const X86_PAGING_INVENTORY *inventory);
#endif
