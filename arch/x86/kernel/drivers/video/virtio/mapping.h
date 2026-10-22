#ifndef X86_VGPU_MAPPING_H
#define X86_VGPU_MAPPING_H
#include "arch/x86/kernel/drivers/video/framebuffer/mapping.h"
#include "arch/x86/mm/paging/gpu/validation.h"
typedef int (*X86_VGPU_CAPTURE)(const KERNEL_BOOT_CONTEXT *, const KERNEL_PHYSICAL_ALLOCATOR *,
    const X86_LAPIC_MAPPING *, X86_PAGING_INVENTORY *);
typedef struct {
    X86_PAGING_GPU_AUTH  Authority;
    BOOT_U64  Self, Budget;
    const KERNEL_BOOT_CONTEXT  *Context;
    X86_PAGE_WINDOW  *Window;
    const X86_LAPIC_MAPPING  *Lapic;
    X86_PAGING_INVENTORY  *Inventory;
    X86_VGPU_CAPTURE  Capture;
    BOOT_U32  Allocated, Pinned, Published, Registered;
} X86_VGPU_MAPPING;
int x86_vgpu_pci_match(const KERNEL_VGPU_BOOT *boot, BOOT_U32 bits);
int x86_vgpu_bind(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory,
    X86_VGPU_CAPTURE capture, X86_VGPU_MAPPING *owner);
int x86_vgpu_unbind(X86_VGPU_MAPPING *owner);
int x86_vgpu_construct(X86_VGPU_MAPPING *owner);
int x86_vgpu_authority(const X86_PAGING_GPU_AUTH *owner);
int x86_vgpu_preflight(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory,
    X86_VGPU_CAPTURE capture, X86_VGPU_MAPPING *owner, X86_FB_CPU *cpu, KERNEL_VGPU_PAGE_PLAN *plan);
#endif
