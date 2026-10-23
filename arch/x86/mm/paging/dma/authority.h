#ifndef X86_DMA_AUTHORITY_H
#define X86_DMA_AUTHORITY_H
#include "kernel/mm/physical/allocator.h"

// Persistent DMA arena: one contiguous pinned RAM lease of 1-16 pages, mapped supervisor RW/XD WB
// at its own PML4 slot with a guard page on each side. Three owned tables (PDPT, PD, PT).
#define X86_DMA_SLOT    229U
#define X86_DMA_VIRTUAL ((229ULL << 39) + 4096)
#define X86_DMA_PAGES   16U
typedef struct {
    BOOT_U64  Owner, Base, Pages, DataId, TableIds[3];
} X86_DMA_SNAPSHOT;
typedef struct {
    BOOT_U64  Self, Root, Pat;
    KERNEL_PHYSICAL_LEASE  Data, Tables[3];
    BOOT_U32  PhysicalBits, Ready;
} X86_DMA_AUTH;
_Static_assert(sizeof(X86_DMA_SNAPSHOT) == 56, "DMA arena snapshot ABI");
typedef int (*X86_DMA_CHECK)(const X86_DMA_AUTH *);

const X86_DMA_AUTH *x86_dma_owner(void);
int x86_dma_register(const X86_DMA_AUTH *expected, const X86_DMA_AUTH *next, X86_DMA_CHECK check);
int x86_dma_inputs(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root, BOOT_U64 output, BOOT_U64 bytes,
    X86_DMA_SNAPSHOT *snapshot);
BOOT_U32 x86_dma_source(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 physical);
BOOT_U32 x86_dma_leaf(BOOT_U64 entry, BOOT_U64 address);
int x86_dma_record(const X86_DMA_SNAPSHOT *snapshot, BOOT_U64 physical, BOOT_U64 view, BOOT_U32 level, BOOT_U32 tag);
int x86_dma_complete(const X86_DMA_SNAPSHOT *snapshot, BOOT_U32 seen, BOOT_U64 leaves);
#endif
