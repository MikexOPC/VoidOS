#ifndef X86_HEAP_BINDING_H
#define X86_HEAP_BINDING_H
#include "arch/x86/kernel/drivers/video/framebuffer/mapping.h"
#include "kernel/mm/heap/heap.h"

// G3: owner of the kernel heap mapping (paging slot 230, see paging/heap/authority.h). Bound once
// at boot before the framebuffer, so every later budget already includes it; never grows (H4).
typedef int (*X86_HEAP_CAPTURE)(const KERNEL_BOOT_CONTEXT *, const KERNEL_PHYSICAL_ALLOCATOR *,
    const X86_LAPIC_MAPPING *, X86_PAGING_INVENTORY *);
typedef struct {
    X86_HEAP_AUTH  Authority;
    BOOT_U64  Self, Budget;
    const KERNEL_BOOT_CONTEXT  *Context;
    X86_PAGE_WINDOW  *Window;
    const X86_LAPIC_MAPPING  *Lapic;
    X86_PAGING_INVENTORY  *Inventory;
    X86_HEAP_CAPTURE  Capture;
    BOOT_U32  DataAllocated, DataPinned, TablesAllocated, TablesPinned, Published, Registered;
} X86_HEAP_BINDING;

int x86_heap_bind(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window, const X86_LAPIC_MAPPING *lapic,
    X86_PAGING_INVENTORY *inventory, X86_HEAP_CAPTURE capture, X86_HEAP_BINDING *owner);
int x86_heap_unbind(X86_HEAP_BINDING *owner);
int x86_heap_check(const X86_HEAP_AUTH *auth);
// Boot entry: binds the mapping, initializes the heap core over it and reports it.
int x86_heap_kernel_start(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory);
// The kernel heap once started, otherwise 0.
KERNEL_HEAP *kernel_heap(void);
// Translates any virtual address inside the kernel heap (including runtime-grown segments) to physical.
int x86_heap_virt_to_phys(uintptr_t virt, uint64_t *phys_out);
// Test image -TestPagingTamper only: one heap PTE changed outside the APIs.
int x86_heap_tamper(void);
#endif
