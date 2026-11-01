#ifndef OS_X86_AP_AUTHORITY_H
#define OS_X86_AP_AUTHORITY_H
#include "../geometry.h"
#include "kernel/mm/physical/allocator.h"
typedef struct {
    BOOT_U64  Owner, Base, Pages, DataId, TableBase, TableId;
    BOOT_U32  Cpus, Tables;
} X86_AP_SNAPSHOT;
typedef struct {
    BOOT_U64  Self, Root, Pat;
    KERNEL_PHYSICAL_LEASE  Data, Tables;
    BOOT_U32  PhysicalBits, Ready, Cpus, Reserved;
} X86_AP_AUTH;
typedef int (*X86_AP_CHECK)(const X86_AP_AUTH *);
const X86_AP_AUTH *x86_ap_owner(void);
int x86_ap_register(const X86_AP_AUTH *expected, const X86_AP_AUTH *next, X86_AP_CHECK check);
int x86_ap_inputs(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root, BOOT_U64 output, BOOT_U64 bytes, X86_AP_SNAPSHOT *snapshot);
BOOT_U32 x86_ap_source(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 physical);
BOOT_U32 x86_ap_leaf(BOOT_U64 entry, BOOT_U64 address);
int x86_ap_record(const X86_AP_SNAPSHOT *snapshot, BOOT_U64 physical, BOOT_U64 view, BOOT_U32 level, BOOT_U32 tag);
int x86_ap_complete(const X86_AP_SNAPSHOT *snapshot, BOOT_U32 seen, BOOT_U64 leaves);
#endif
