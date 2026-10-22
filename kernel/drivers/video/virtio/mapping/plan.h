#ifndef KERNEL_VGPU_MAPPING_PLAN_H
#define KERNEL_VGPU_MAPPING_PLAN_H
#include "../discovery/handoff.h"

#define KERNEL_VGPU_MAX_PAGES 16U
typedef struct {
    uint64_t  Address[4], Length[4];
} KERNEL_VGPU_RANGES;
typedef struct {
    uint64_t  Physical[16], Offset[4];
    uint32_t  Count, Valid;
} KERNEL_VGPU_PAGE_PLAN;
_Static_assert(sizeof(KERNEL_VGPU_RANGES) == 64, "Range ABI");
_Static_assert(sizeof(KERNEL_VGPU_PAGE_PLAN) == 168, "Page plan ABI");
KERNEL_VGPU_PAGE_PLAN kernel_vgpu_pages(KERNEL_VGPU_RANGES ranges, uint32_t bits);
int kernel_vgpu_plan(const KERNEL_VGPU_BOOT *boot, uint32_t bits, KERNEL_VGPU_PAGE_PLAN *output);
#endif
