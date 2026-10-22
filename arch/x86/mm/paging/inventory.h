#ifndef OS_X86_PAGING_INVENTORY_H
#define OS_X86_PAGING_INVENTORY_H
#include "kernel/mm/physical/allocator.h"
#include "arch/x86/configs/boot_profile.h"
#include "gpu/authority.h"
#include "surface/authority.h"
#include "iommu/authority.h"
typedef struct { BOOT_U64 Physical, View; BOOT_U32 Level, Reserved; } X86_PAGING_RESOURCE;
typedef struct { BOOT_U64 Virtual, Physical, Cache, Owner; } X86_PAGING_DEVICE;
struct X86_LAPIC_MAPPING;
#define X86_PAGING_VIDEO_TABLES 11U
typedef struct {
    BOOT_U64  Virtual, Physical, Pages, AperturePages, Cache, Pat, Owner;
    BOOT_U32  TableCount, Reserved;
} X86_PAGING_VIDEO_RECORD;
typedef struct {
    BOOT_U64  Self, Root;
    X86_PAGING_VIDEO_RECORD  Region;
    BOOT_U32  PhysicalBits, Ready;
    KERNEL_PHYSICAL_LEASE  Tables[X86_PAGING_VIDEO_TABLES];
} X86_PAGING_VIDEO_AUTH;
typedef int (*X86_PAGING_VIDEO_CHECK)(const X86_PAGING_VIDEO_AUTH *owner);
typedef struct {
    BOOT_U64 Root, LeafPages;
    BOOT_U32 Count, Ready;
    X86_PAGING_RESOURCE Tables[X86_BOOT_TABLE_LIMIT];
    BOOT_U64 RamPages, MmioPages;
    X86_PAGING_DEVICE Device;
    X86_PAGING_VIDEO_RECORD Video;
    BOOT_U64 VideoTableIds[X86_PAGING_VIDEO_TABLES];
    X86_PAGING_GPU_AUTH Gpu;
    X86_SURFACE_SNAPSHOT Surface;
    X86_IOMMU_SNAPSHOT Iommu;
} X86_PAGING_INVENTORY;
_Static_assert(sizeof(X86_PAGING_INVENTORY) == 3840, "Registro RAM MMIO bounded");
_Static_assert(sizeof(X86_PAGING_VIDEO_AUTH) == 440, "Autorita video bounded");
// ====== backend in sola lettura e matematica scalare verificata da Rust ===== //

const volatile BOOT_U64 *x86_paging_view(BOOT_U64 address);
BOOT_U64 x86_paging_child_view(BOOT_U64 parent, BOOT_U32 level, BOOT_U32 slot);
BOOT_U32 x86_paging_entry_valid(BOOT_U64 entry, BOOT_U32 level, BOOT_U32 bits);
BOOT_U64 x86_paging_leaf_virtual(BOOT_U64 view, BOOT_U32 slot);
BOOT_U32 x86_paging_leaf_kind(BOOT_U64 entry, BOOT_U64 virtual_address,
    BOOT_U64 device_physical, BOOT_U64 cache, BOOT_U32 bits);
BOOT_U32 x86_paging_video_policy(BOOT_U64 address, BOOT_U64 physical, BOOT_U64 pages,
    BOOT_U64 aperture_pages, BOOT_U64 cache, BOOT_U64 pat, BOOT_U32 bits);
BOOT_U32 x86_paging_video_leaf(BOOT_U64 entry, BOOT_U64 address, BOOT_U64 physical,
    BOOT_U64 virtual_base, BOOT_U64 pages, BOOT_U64 aperture_pages, BOOT_U64 cache,
    BOOT_U32 bits);
int x86_paging_inventory_capture_video(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const struct X86_LAPIC_MAPPING *lapic,
    const X86_PAGING_VIDEO_AUTH *video, X86_PAGING_VIDEO_CHECK check,
    X86_PAGING_INVENTORY *output);
int x86_paging_inventory_verify_video(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const struct X86_LAPIC_MAPPING *lapic,
    const X86_PAGING_VIDEO_AUTH *video, X86_PAGING_VIDEO_CHECK check,
    const X86_PAGING_INVENTORY *inventory);
int x86_paging_inventory_capture_lapic(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const struct X86_LAPIC_MAPPING *owner,
    X86_PAGING_INVENTORY *output);
int x86_paging_inventory_verify_lapic(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const struct X86_LAPIC_MAPPING *owner,
    const X86_PAGING_INVENTORY *inventory);
int x86_paging_inventory_capture(const KERNEL_BOOT_CONTEXT *context,
                                  const KERNEL_PHYSICAL_ALLOCATOR *pool, X86_PAGING_INVENTORY *output);
int x86_paging_inventory_verify(const KERNEL_BOOT_CONTEXT *context,
                                 const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_PAGING_INVENTORY *inventory);
#endif
