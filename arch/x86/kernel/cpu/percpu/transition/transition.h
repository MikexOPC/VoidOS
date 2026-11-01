#ifndef OS_X86_AP_TRANSITION_H
#define OS_X86_AP_TRANSITION_H
#include "../lowmemory/lowmemory.h"
#include "arch/x86/configs/layout.h"

typedef struct {
    int (*Transfer)(void *context, BOOT_U64 physical, BOOT_U64 *page, int write);
    int (*Safe)(void *context);
    void  *Context;
} XT_IO;

typedef struct {
    BOOT_U64  Self;
    KERNEL_PHYSICAL_ALLOCATOR  *Pool;
    const X86_AP_LOW_MEMORY  *Code;
    KERNEL_PHYSICAL_LEASE  Data, Tables;
    BOOT_U64  Root, Pat, Budget;
    XT_IO  Io;
    BOOT_U32  State, DataPinned, TablesPinned, Reserved;
    BOOT_U64  Source[512];
    BOOT_U64  Scratch[512];
} X86_AP_TRANSITION;
_Static_assert(offsetof(X86_AP_TRANSITION, Source) == 152, "Transition snapshot offset");

BOOT_U32 xt_geometry(BOOT_U64 code, BOOT_U64 data, BOOT_U64 tables);
BOOT_U32 xt_source_entry(BOOT_U64 entry, BOOT_U32 bits);
BOOT_U64 xt_entry(BOOT_U32 table, BOOT_U32 index, BOOT_U64 tables, BOOT_U64 code,
    BOOT_U64 data, BOOT_U64 source, BOOT_U32 recursive);
int xt_early(KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_AP_LOW_MEMORY *code, X86_AP_TRANSITION *owner);
int xt_prepare(X86_AP_TRANSITION *owner, BOOT_U64 root, BOOT_U64 pat, const XT_IO *io);
int xt_verify(X86_AP_TRANSITION *owner);
int xt_verify_tables(X86_AP_TRANSITION *owner);
int xt_cancel(X86_AP_TRANSITION *owner);
int xt_build(X86_AP_TRANSITION *owner);
int xt_freeze(X86_AP_TRANSITION *owner);
int x86_ap_transition_early(KERNEL_PHYSICAL_ALLOCATOR *pool);
#endif
