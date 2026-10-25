#ifndef X86_SURFACE_AUTHORITY_H
#define X86_SURFACE_AUTHORITY_H
#include "kernel/mm/physical/allocator.h"
#include "arch/x86/configs/layout.h"
#define X86_SURFACE_SLOT X86_LAYOUT_SURFACE_SLOT
#define X86_SURFACE_VIRTUAL (X86_LAYOUT_SLOT_ADDRESS(X86_SURFACE_SLOT) + 4096)
#define X86_SURFACE_TABLES 11U
typedef struct {
    BOOT_U64  Owner, Base, Pages, DataId, TableIds[11];
} X86_SURFACE_SNAPSHOT;
typedef struct {
    BOOT_U64  Self, Root, Pat, Bytes;
    KERNEL_PHYSICAL_LEASE  Data, Tables[11];
    BOOT_U32  Count, PhysicalBits, Ready, Reserved;
} X86_SURFACE_AUTH;
typedef int (*X86_SURFACE_CHECK)(const X86_SURFACE_AUTH *);
const X86_SURFACE_AUTH *x86_surface_owner(void);
int x86_surface_register(const X86_SURFACE_AUTH *expected,const X86_SURFACE_AUTH *next,X86_SURFACE_CHECK check);
int x86_surface_inputs(const KERNEL_PHYSICAL_ALLOCATOR *pool,BOOT_U64 root,BOOT_U64 output,
    BOOT_U64 bytes,X86_SURFACE_SNAPSHOT *snapshot);
BOOT_U32 x86_surface_source(const KERNEL_PHYSICAL_ALLOCATOR *pool,BOOT_U64 physical);
BOOT_U32 x86_surface_leaf(BOOT_U64 entry,BOOT_U64 address);
int x86_surface_record(const X86_SURFACE_SNAPSHOT *snapshot,BOOT_U64 physical,BOOT_U64 view,BOOT_U32 level,BOOT_U32 tag);
int x86_surface_complete(const X86_SURFACE_SNAPSHOT *snapshot,BOOT_U32 seen,BOOT_U64 leaves);
#endif
