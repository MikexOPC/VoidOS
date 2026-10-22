#ifndef X86_VGPU_SURFACE_MAPPING_H
#define X86_VGPU_SURFACE_MAPPING_H
#include "../mapping.h"
#include "kernel/drivers/video/virtio/resources/resource.h"
typedef struct {
    X86_SURFACE_AUTH  Authority;
    X86_VGPU_MAPPING  *Gpu;
    BOOT_U64  Self, Budget;
    BOOT_U32  Allocated, Pinned, Published, Registered, DataAllocated, DataPinned;
} X86_SURFACE_MAPPING;
int x86_vgpu_surface_bind(X86_VGPU_MAPPING *gpu,uint32_t width,uint32_t height,X86_SURFACE_MAPPING *owner);
int x86_vgpu_surface_unbind(X86_SURFACE_MAPPING *owner);
int x86_vgpu_surface_construct(X86_SURFACE_MAPPING *owner);
int x86_vgpu_surface_check(const X86_SURFACE_AUTH *auth);
volatile uint32_t *x86_vgpu_surface_pixels(X86_SURFACE_MAPPING *owner);
#endif
