#ifndef X86_IOMMU_AUTHORITY_H
#define X86_IOMMU_AUTHORITY_H
#include "kernel/mm/physical/allocator.h"
#define X86_IOMMU_SLOT 228U
#define X86_IOMMU_VIRTUAL ((228ULL<<39)+4096)
typedef struct {
    BOOT_U64  Owner, Cache, Base[8], TableIds[3], Writable;
    BOOT_U32  Count, Reserved;
} X86_IOMMU_SNAPSHOT;
typedef struct {
    BOOT_U64  Self, Root, Pat, Cache, Base[8], Writable;
    KERNEL_PHYSICAL_LEASE  Tables[3];
    BOOT_U32  Count, PhysicalBits, Ready, Reserved;
} X86_IOMMU_AUTH;
_Static_assert(sizeof(X86_IOMMU_SNAPSHOT)==120,"IOMMU snapshot ABI");
typedef int (*X86_IOMMU_CHECK)(const X86_IOMMU_AUTH *);
const X86_IOMMU_AUTH *x86_iommu_owner(void);
int x86_iommu_register(const X86_IOMMU_AUTH *expected,const X86_IOMMU_AUTH *next,X86_IOMMU_CHECK check);
int x86_iommu_page_safe(const KERNEL_BOOT_CONTEXT *context,const KERNEL_PHYSICAL_ALLOCATOR *pool,BOOT_U64 physical);
int x86_iommu_inputs(const KERNEL_BOOT_CONTEXT *context,const KERNEL_PHYSICAL_ALLOCATOR *pool,
    BOOT_U64 output,BOOT_U64 bytes,X86_IOMMU_SNAPSHOT *snapshot);
BOOT_U32 x86_iommu_source(const KERNEL_PHYSICAL_ALLOCATOR *pool,BOOT_U64 physical);
BOOT_U32 x86_iommu_leaf(BOOT_U64 entry,BOOT_U64 address);
int x86_iommu_record(const X86_IOMMU_SNAPSHOT *snapshot,BOOT_U64 physical,BOOT_U64 view,BOOT_U32 level,BOOT_U32 tag);
int x86_iommu_complete(const X86_IOMMU_SNAPSHOT *snapshot,BOOT_U32 seen,BOOT_U32 leaves);
#endif
