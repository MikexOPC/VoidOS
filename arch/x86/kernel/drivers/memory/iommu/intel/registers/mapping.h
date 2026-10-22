#ifndef X86_VTD_REGISTERS_MAPPING_H
#define X86_VTD_REGISTERS_MAPPING_H
#include "../discovery/table.h"
#include "arch/x86/kernel/drivers/video/framebuffer/mapping.h"
#include "arch/x86/mm/paging/iommu/authority.h"
typedef struct {
    X86_IOMMU_AUTH  Authority;
    BOOT_U64  Self, Budget, Span[8], Consumer;
    const KERNEL_BOOT_CONTEXT  *Context;
    const X86_LAPIC_MAPPING  *Lapic;
    X86_PAGE_WINDOW  *Window;
    X86_PAGING_INVENTORY  *Inventory;
    BOOT_U32  Allocated, Pinned, Published, Registered;
} X86_VTD_MAPPING;
typedef struct {
    BOOT_U64  Cap, Ecap, Span;
    BOOT_U32  Version, Status, AddressBits, Domains, IotlbOffset, FaultOffset, FaultCount, Compatible;
} X86_VTD_CAPABILITIES;
_Static_assert(sizeof(X86_VTD_CAPABILITIES)==56,"VT-d capability ABI");
BOOT_U64 x86_vtd_entry(BOOT_U32 table,BOOT_U32 slot,BOOT_U64 base,BOOT_U64 cache,BOOT_U64 child);
BOOT_U64 x86_vtd_entry_access(BOOT_U32 table,BOOT_U32 slot,BOOT_U64 base,BOOT_U64 cache,BOOT_U64 child,BOOT_U32 writable);
BOOT_U32 x86_vtd_capability_policy(BOOT_U32 version,BOOT_U64 cap,BOOT_U64 ecap,BOOT_U64 span,BOOT_U32 status);
int x86_vtd_mapping_bind(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,const X86_DMAR_INFO *description,
    X86_PAGING_INVENTORY *inventory,X86_VTD_MAPPING *owner);
int x86_vtd_mapping_bind_access(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,const X86_DMAR_INFO *description,
    X86_PAGING_INVENTORY *inventory,X86_VTD_MAPPING *owner,BOOT_U32 writable);
int x86_vtd_mapping_unbind(X86_VTD_MAPPING *owner);
int x86_vtd_mapping_check(const X86_IOMMU_AUTH *authority);
int x86_vtd_mapping_preflight(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,const X86_DMAR_INFO *description,
    X86_PAGING_INVENTORY *inventory,X86_VTD_MAPPING *owner,X86_FB_CPU *cpu);
int x86_vtd_mapping_construct(X86_VTD_MAPPING *owner);
int x86_vtd_inventory_capture(const X86_VTD_MAPPING *owner);
int x86_vtd_inventory_verify(const X86_VTD_MAPPING *owner);
int x86_vtd_capabilities_read(X86_VTD_MAPPING *owner,BOOT_U32 unit,X86_VTD_CAPABILITIES *output);
volatile unsigned char *x86_vtd_register_view(BOOT_U64 address,BOOT_U32 width);
#endif
