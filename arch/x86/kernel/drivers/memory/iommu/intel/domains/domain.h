#ifndef X86_VTD_DOMAIN_H
#define X86_VTD_DOMAIN_H
#include "../registers/mapping.h"
#define X86_VTD_DMA_BASE 0x01000000ULL
#define X86_VTD_DMA_BYTES 0x02000000ULL
#define X86_VTD_DMA_RANGES 16U
#define X86_VTD_DMA_PAGES 4100U
#define X86_VTD_DOMAIN_TABLES 14U

typedef struct {
    KERNEL_PHYSICAL_LEASE  Lease;
    BOOT_U32  Access, Reserved;
} X86_VTD_BUFFER;

typedef struct {
    BOOT_U32  Segment, Bus, Device, Function, PhysicalBits, Count;
    X86_VTD_BUFFER  Buffers[X86_VTD_DMA_RANGES];
} X86_VTD_DOMAIN_REQUEST;

typedef struct {
    BOOT_U64  Self, Budget, Root;
    const KERNEL_BOOT_CONTEXT  *Context;
    const X86_LAPIC_MAPPING  *Lapic;
    X86_PAGE_WINDOW  *Window;
    X86_PAGING_INVENTORY  *Inventory;
    X86_VTD_DOMAIN_REQUEST  Request;
    BOOT_U64  Iova[X86_VTD_DMA_RANGES];
    KERNEL_PHYSICAL_LEASE  Tables[X86_VTD_DOMAIN_TABLES];
    BOOT_U32  TableCount, Allocated, Pins, State, Revoked;
} X86_VTD_DOMAIN;
BOOT_U64 x86_vtd_domain_next(BOOT_U64 cursor,BOOT_U64 pages,BOOT_U64 total);
BOOT_U64 x86_vtd_domain_word(BOOT_U32 kind,BOOT_U32 slot,BOOT_U32 selector,BOOT_U64 child,
    BOOT_U64 physical,BOOT_U32 access,BOOT_U32 bits);
int x86_vtd_domain_prepare(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,
    X86_PAGING_INVENTORY *inventory,const X86_VTD_DOMAIN_REQUEST *request,X86_VTD_DOMAIN *owner);
int x86_vtd_domain_preflight(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,
    X86_PAGING_INVENTORY *inventory,const X86_VTD_DOMAIN_REQUEST *request,X86_VTD_DOMAIN *owner);
int x86_vtd_domain_inputs(const X86_VTD_DOMAIN *owner);
int x86_vtd_domain_pages(X86_VTD_DOMAIN *owner,int write);
BOOT_U64 x86_vtd_domain_expected(const X86_VTD_DOMAIN *owner,BOOT_U32 table,BOOT_U32 slot);
int x86_vtd_domain_verify(X86_VTD_DOMAIN *owner);
int x86_vtd_domain_release(X86_VTD_DOMAIN *owner);
// Clears the second-stage leaves of one buffer (bit index); the caller flushes CPU caches and the IOTLB.
int x86_vtd_domain_revoke(X86_VTD_DOMAIN *owner,BOOT_U32 index);
// IOVA of [physical, physical+bytes) inside one live (not revoked) buffer that grants access
// (1 read, 3 read/write); 0 when the range is not entirely covered. Pure lookup, no table access.
BOOT_U64 x86_vtd_domain_translate(const X86_VTD_DOMAIN *owner,BOOT_U64 physical,BOOT_U64 bytes,BOOT_U32 access);
#endif
