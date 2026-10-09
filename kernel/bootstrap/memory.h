#ifndef OS_KERNEL_BOOTSTRAP_MEMORY_H
#define OS_KERNEL_BOOTSTRAP_MEMORY_H
#include "kernel/boot/contract.h"
#include "kernel/mm/inventory.h"
#include "arch/x86/mm/window/page.h"
#include "arch/x86/mm/arena/objects.h"

// ====== bootstrap_owned_memory ======

// Static lifetime across the loader-stack retirement. Handles never escape their
// owner; mappings and the two persistent objects remain live after initialization.
typedef struct {
    KERNEL_BOOT_CONTEXT        Context;
    KERNEL_MEMORY_INVENTORY    Inventory;
    KERNEL_PHYSICAL_ALLOCATOR  Physical;
    X86_PAGE_WINDOW            Window;
    X86_REGION_MAPPER          Regions;
    X86_REGION_HANDLE          Handles[2];
    X86_OBJECT_ARENA           Arena;
    KERNEL_OBJECT_HANDLE       ContextObject;
    KERNEL_OBJECT_HANDLE       InventoryObject;
} KERNEL_BOOTSTRAP_MEMORY;

void kernel_bootstrap_memory_prepare(KERNEL_BOOTSTRAP_MEMORY *memory);
#endif
