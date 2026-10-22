#ifndef X86_PAGING_GPU_AUTHORITY_H
#define X86_PAGING_GPU_AUTHORITY_H
#include "kernel/drivers/video/virtio/mapping/plan.h"
#include "kernel/mm/physical/allocator.h"
#define X86_GPU_SLOT 226U
#define X86_GPU_VIRTUAL ((226ULL << 39)+4096)
#define X86_GPU_TABLES 3U
typedef struct {
    BOOT_U64  Self, Root, Pat, Cache;
    KERNEL_VGPU_PAGE_PLAN  Plan;
    KERNEL_PHYSICAL_LEASE  Tables[3];
    BOOT_U32  PhysicalBits, Ready;
} X86_PAGING_GPU_AUTH;
_Static_assert(sizeof(X86_PAGING_GPU_AUTH) == 304, "Bounded GPU authority record");
typedef int (*X86_PAGING_GPU_CHECK)(const X86_PAGING_GPU_AUTH *);
const X86_PAGING_GPU_AUTH *x86_paging_gpu_owner(void);
int x86_paging_gpu_live(void);
int x86_paging_gpu_register(const X86_PAGING_GPU_AUTH *expected, const X86_PAGING_GPU_AUTH *next,
    X86_PAGING_GPU_CHECK check);
#endif
