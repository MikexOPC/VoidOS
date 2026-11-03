#ifndef X86_DMA_ARENA_H
#define X86_DMA_ARENA_H
#include "arch/x86/kernel/drivers/video/framebuffer/mapping.h"

// Owner of the persistent DMA arena (paging slot 229, see paging/dma/authority.h). Unlike the
// page window it stays mapped for a whole session, so window borrows keep working beside it.
typedef int (*X86_DMA_CAPTURE)(const KERNEL_BOOT_CONTEXT *, const KERNEL_PHYSICAL_ALLOCATOR *,
    const X86_LAPIC_MAPPING *, X86_PAGING_INVENTORY *);
typedef struct {
    X86_DMA_AUTH  Authority;
    BOOT_U64  Self, Budget;
    const KERNEL_BOOT_CONTEXT  *Context;
    X86_PAGE_WINDOW  *Window;
    const X86_LAPIC_MAPPING  *Lapic;
    X86_PAGING_INVENTORY  *Inventory;
    X86_DMA_CAPTURE  Capture;
    BOOT_U32  Allocated, Pinned, Published, Registered, DataAllocated, DataPinned;
} X86_DMA_ARENA;

int x86_dma_arena_bind(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window, const X86_LAPIC_MAPPING *lapic,
    X86_PAGING_INVENTORY *inventory, X86_DMA_CAPTURE capture, BOOT_U32 pages, X86_DMA_ARENA *owner);
// The caller must quiesce the device and revoke any DMA translations before unbind.
int x86_dma_arena_unbind(X86_DMA_ARENA *owner);
int x86_dma_arena_check(const X86_DMA_AUTH *auth);
// Zeroed when bound; 0 unless the arena is registered and its paging is current.
volatile unsigned char *x86_dma_arena_bytes(X86_DMA_ARENA *owner);
#endif
